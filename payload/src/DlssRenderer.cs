using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using BepInEx;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.PostProcessing;
using Object = UnityEngine.Object;

namespace NightShift;

public enum DlssMode { Off, Quality, Balanced, Performance, DLAA }

internal static class DlssRenderer
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Frame
    {
        public uint size, version;
        public IntPtr color, depth, motion, output;
        public uint inputWidth, inputHeight, outputWidth, outputHeight;
        public int quality, reset, reversedDepth, hdr;
        public float jitterX, jitterY, motionScaleX, motionScaleY, deltaMs;
        public uint reserved;
    }
    private const string NativeName="NightShiftDLSS";
    [DllImport(NativeName,CallingConvention=CallingConvention.Cdecl)] private static extern IntPtr NS_GetRenderEvent();
    [DllImport(NativeName,CallingConvention=CallingConvention.Cdecl)] private static extern IntPtr NS_CreatePacket(ref Frame frame);
    [DllImport(NativeName,CallingConvention=CallingConvention.Cdecl)] private static extern void NS_GetStatus(StringBuilder buffer,uint capacity,out ulong frames,out uint result);
    private static IntPtr renderEvent;
    private static Camera? camera;
    private static PostProcessLayer? layer;
    private static RenderTexture? sceneTarget, output, colorCopy, depthCopy, motionCopy;
    private static Camera? presentation;
    private static GameObject? presentationObject;
    private static CommandBuffer? presentBuffer;
    private static CommandBuffer? captureBuffers;
    private static IntPtr colorPtr, depthPtr, motionPtr, outputPtr;
    private static bool oldMsaa, oldFinalBlit, oldTransparentJitter;
    private static DepthTextureMode oldDepth;
    private static PostProcessLayer.Antialiasing oldAa;
    private static Matrix4x4 unjittered;
    private static float originalAspect;
    private static Vector2 jitter;
    private static Vector3 lastPosition;
    private static Quaternion lastRotation;
    private static float lastFov, lastRenderTime;
    private static int sequence;
    private static bool reset=true, projectionOwned;
    private static ulong activationFrameCount;
    private static int ow,oh,iw,ih;
    private static DlssMode appliedMode;
    private static bool available;
    private static bool quitting;
    private static DlssMode configuredMode;
    private static Harmony? renderHooks;
    private static bool reportedActive;
    internal static string Status="DLAA starting";
    internal static string MenuStatus => Plugin.Instance.RenderMode.Value==DlssMode.Off
        ? "DLAA: Off"
        : !available ? "DLAA: Unavailable"
        : IsActive ? "DLAA: On (native resolution)" : "DLAA: Starting";
    internal static ulong SuccessfulFrames;
    internal static bool IsActive => camera!=null && SuccessfulFrames>activationFrameCount && Plugin.Instance.RenderMode.Value!=DlssMode.Off;
    internal static string InputResolution => colorCopy==null ? "native" : $"{iw}x{ih}";
    internal static Camera? PresentationCamera => presentation;
    internal static int SceneInputWidth => camera==null?Screen.width:iw;
    internal static int SceneInputHeight => camera==null?Screen.height:ih;
    internal static Matrix4x4? UnjitteredProjection => camera==null || !projectionOwned?null:unjittered;
    internal static Vector2? CameraJitter => camera==null?null:jitter;

    internal static void Install(Harmony hooks)
    {
        if(SystemInfo.graphicsDeviceType!=GraphicsDeviceType.Direct3D11)
        { Plugin.Instance.RenderMode.Value=DlssMode.Off; Status="DLSS unavailable: this bridge requires Direct3D 11"; return; }
        if(Marshal.SizeOf<Frame>()!=96) throw new InvalidOperationException("DLSS packet ABI mismatch");
        string dll=Path.Combine(Paths.BepInExRootPath,"NightShift","native","NightShiftDLSS.dll");
        if(!File.Exists(dll) || !File.Exists(Path.Combine(Path.GetDirectoryName(dll)!,"nvngx_dlss.dll")))
        { Status="DLSS unavailable: native bridge/runtime missing"; return; }
        IntPtr handle=NativeLibrary.Load(dll);
        NativeLibrary.SetDllImportResolver(typeof(DlssRenderer).Assembly,(name,assembly,path)=>name==NativeName?handle:IntPtr.Zero);
        renderEvent=NS_GetRenderEvent();
        available=renderEvent!=IntPtr.Zero;
        renderHooks=new Harmony(Plugin.Id+".dlss");
        try
        {
        renderHooks.Patch(AccessTools.DeclaredMethod(typeof(Camera),"FireOnPreCull"),
            prefix:new HarmonyMethod(typeof(DlssRenderer),nameof(CameraPreparing)));
        renderHooks.Patch(AccessTools.DeclaredMethod(typeof(PostProcessLayer),"OnPreCull"),
            prefix:new HarmonyMethod(typeof(DlssRenderer),nameof(BeforeCull)),
            postfix:new HarmonyMethod(typeof(DlssRenderer),nameof(AfterCull)));
        renderHooks.Patch(AccessTools.DeclaredMethod(typeof(PostProcessLayer),"OnRenderImage"),
            prefix:new HarmonyMethod(typeof(DlssRenderer),nameof(RenderImage)));
        renderHooks.Patch(AccessTools.DeclaredMethod(typeof(PostProcessLayer),"OnPostRender"),
            postfix:new HarmonyMethod(typeof(DlssRenderer),nameof(AfterRender)));
        renderHooks.Patch(AccessTools.DeclaredMethod(typeof(PostProcessLayer),"OnDisable"),
            prefix:new HarmonyMethod(typeof(DlssRenderer),nameof(LayerDisabled)));
        Plugin.Instance.Log.LogInfo("Native-resolution DLAA bridge loaded; F5 toggles DLAA. DLSS upscaling is coming soon.");
        Plugin.Instance.Log.LogInfo("Configured rendering mode: "+Plugin.Instance.RenderMode.Value);
        configuredMode=Plugin.Instance.RenderMode.Value;
        }
        catch(Exception ex)
        {
            renderHooks.UnpatchSelf(); renderHooks=null; available=false;
            Plugin.Instance.RenderMode.Value=DlssMode.Off;
            Status="DLSS unavailable: render hooks failed: "+ex.Message;
            Plugin.Instance.Log.LogError(Status);
        }
    }
    internal static void Choose(DlssMode mode)
    {
        if(mode!=DlssMode.Off && mode!=DlssMode.DLAA) mode=DlssMode.DLAA;
        Restore();
        if(mode!=DlssMode.Off && !available) { Status="DLSS unavailable: native bridge/runtime missing"; return; }
        Plugin.Instance.RenderMode.Value=mode;
        configuredMode=mode;
        Status=mode==DlssMode.Off ? "DLAA OFF; native rendering restored" : "DLAA requested; waiting for the main camera";
        Plugin.Instance.Log.LogInfo(Status);
    }
    internal static void BeginFrame()
    {
        if(quitting) return;
        if(Plugin.Instance.RenderMode.Value!=configuredMode) Choose(Plugin.Instance.RenderMode.Value);
        // UI, camera controls and other cameras see the original viewport between renders.
        RestoreCameraTarget();
        if(camera!=null && !FrameGenRenderer.IsGameplayReady()) Restore();
        if(camera!=null && camera!=Camera.main) Restore();
    }
    internal static void PrepareFrame()
    {
        if(quitting || !available || Plugin.Instance.RenderMode.Value==DlssMode.Off || !FrameGenRenderer.IsGameplayReady()) return;
        var main=Camera.main;
        if(main==null) return;
        var effects=main.GetComponent<PostProcessLayer>();
        // Unity snapshots the scene target before FireOnPreCull. Setting the
        // target in LateUpdate supplies the real dimensions to frame allocation.
        if(effects!=null && effects.enabled) BeforeCull(effects);
    }
    private static void CameraPreparing(Camera __0)
    {
        // The next camera begins only after the scene camera has completed its
        // native render/image effects. Restore here, rather than inside an
        // OnRenderImage callback while Unity still owns the scene target.
        if(camera!=null && __0==presentation) RestoreCameraTarget();
        if(quitting || !available || Plugin.Instance.RenderMode.Value==DlssMode.Off || !FrameGenRenderer.IsGameplayReady() || __0==null || __0!=Camera.main) return;
        try
        {
            var effects=__0.GetComponent<PostProcessLayer>();
            if(effects!=null && effects.enabled) BeforeCull(effects);
        }
        catch(Exception ex) { Fail(ex.Message); }
    }
    private static void BeforeCull(PostProcessLayer __instance)
    {
        if(quitting || !available || Plugin.Instance.RenderMode.Value==DlssMode.Off || !FrameGenRenderer.IsGameplayReady()) return;
        try
        {
            var candidate=__instance.m_Camera;
            if(candidate==null || candidate!=Camera.main) return;
            DlssMode requested=Plugin.Instance.RenderMode.Value;
            if(camera!=candidate || appliedMode!=requested || ow!=Screen.width || oh!=Screen.height)
            {
                Restore();
                if(candidate.targetTexture!=null) { Fail("Main camera already owns a render target; refusing to replace another rendering path"); return; }
                if(candidate.stereoEnabled || candidate.orthographic || candidate.rect.width!=1 || candidate.rect.height!=1)
                { Fail("Unsupported stereo/orthographic/cropped main camera"); return; }
                camera=candidate; layer=__instance; appliedMode=requested;
                oldMsaa=camera.allowMSAA; oldDepth=camera.depthTextureMode;
                originalAspect=camera.aspect;
                oldTransparentJitter=camera.useJitteredProjectionMatrixForTransparentRendering;
                oldAa=layer.antialiasingMode; oldFinalBlit=layer.finalBlitToCameraTarget;
                ow=Screen.width; oh=Screen.height;
                double scale=requested==DlssMode.DLAA?1:requested==DlssMode.Quality?2.0/3:requested==DlssMode.Balanced?.58:.5;
                iw=Math.Max(32,(int)Math.Round(ow*scale)); ih=Math.Max(32,(int)Math.Round(oh*scale));
                if(requested!=DlssMode.DLAA)
                {
                    sceneTarget=new RenderTexture(iw,ih,24,RenderTextureFormat.ARGBHalf,RenderTextureReadWrite.Linear)
                        {name="NightShift low-resolution scene",antiAliasing=1,useMipMap=false};
                    if(!sceneTarget.Create()) throw new InvalidOperationException("Could not allocate scene render target");
                    presentationObject=new GameObject("NightShift DLSS presentation");
                    Object.DontDestroyOnLoad(presentationObject);
                    presentation=presentationObject.AddComponent<Camera>();
                    presentation.cullingMask=0;
                    presentation.clearFlags=CameraClearFlags.Nothing;
                    presentation.allowHDR=false; presentation.allowMSAA=false;
                    presentation.orthographic=true;
                    presentation.depth=camera.depth+.01f;
                    presentBuffer=new CommandBuffer{name="NightShift full-resolution presentation"};
                    presentation.AddCommandBuffer(CameraEvent.AfterEverything,presentBuffer);
                }
                output=new RenderTexture(ow,oh,0,RenderTextureFormat.ARGBHalf,RenderTextureReadWrite.Linear)
                    {name="NightShift DLSS output",enableRandomWrite=true,antiAliasing=1,useMipMap=false};
                colorCopy=new RenderTexture(iw,ih,0,RenderTextureFormat.ARGBHalf,RenderTextureReadWrite.Linear)
                    {name="NightShift pre-UI color",antiAliasing=1,useMipMap=false};
                depthCopy=new RenderTexture(iw,ih,0,RenderTextureFormat.RFloat,RenderTextureReadWrite.Linear)
                    {name="NightShift copied camera depth",antiAliasing=1,useMipMap=false};
                motionCopy=new RenderTexture(iw,ih,0,RenderTextureFormat.RGHalf,RenderTextureReadWrite.Linear)
                    {name="NightShift copied object motion",antiAliasing=1,useMipMap=false};
                if(!output.Create() || !colorCopy.Create() || !depthCopy.Create() || !motionCopy.Create())
                    throw new InvalidOperationException("Could not allocate DLSS textures");
                colorPtr=colorCopy.GetNativeTexturePtr(); depthPtr=depthCopy.GetNativeTexturePtr();
                motionPtr=motionCopy.GetNativeTexturePtr(); outputPtr=output.GetNativeTexturePtr();
                // Built-in engine buffers need not be exposed as managed Shader.GetGlobalTexture objects.
                // Copy while this camera's buffers are bound, before legacy OnRenderImage runs.
                captureBuffers=new CommandBuffer{name="NightShift same-camera depth and motion capture"};
                captureBuffers.Blit(new RenderTargetIdentifier(BuiltinRenderTextureType.Depth),new RenderTargetIdentifier(depthCopy));
                captureBuffers.Blit(new RenderTargetIdentifier(BuiltinRenderTextureType.MotionVectors),new RenderTargetIdentifier(motionCopy));
                camera.AddCommandBuffer(CameraEvent.BeforeImageEffects,captureBuffers);
                var native=ReadStatus(); activationFrameCount=native.frames;
                reset=true; sequence=0; reportedActive=false; lastFov=camera.fieldOfView;
                lastPosition=camera.transform.position; lastRotation=camera.transform.rotation;
                Plugin.Instance.Log.LogInfo($"DLSS {requested} camera {camera.name}: {iw}x{ih} -> {ow}x{oh}; gameplay and crowd updates retained.");
            }
            if(camera==null || layer==null) return;
            if(camera.targetTexture!=null && camera.targetTexture!=sceneTarget)
            { Fail("Game changed its camera target; restoring its rendering path"); return; }
            // Render the scene into an actual lower-resolution target. A later,
            // empty camera owns full-resolution presentation. No target is
            // rebound in OnRenderImage and no global screen resolution changes.
            if(sceneTarget!=null)
            {
                camera.targetTexture=sceneTarget;
                camera.aspect=originalAspect;
                if(presentation!=null) presentation.depth=camera.depth+.01f;
            }
            camera.allowMSAA=false;
            camera.depthTextureMode |= DepthTextureMode.Depth | DepthTextureMode.MotionVectors;
            camera.useJitteredProjectionMatrixForTransparentRendering=true;
            layer.antialiasingMode=PostProcessLayer.Antialiasing.None;
            layer.finalBlitToCameraTarget=false;
        }
        catch(Exception ex) { Fail(ex.Message); }
    }
    private static void AfterCull(PostProcessLayer __instance)
    {
        if(camera==null || __instance!=layer || Plugin.Instance.RenderMode.Value==DlssMode.Off) return;
        try
        {
            // Applied after the game's post-processing setup so its TAA cannot add a second jitter.
            unjittered=camera.projectionMatrix;
            camera.nonJitteredProjectionMatrix=unjittered;
            int phase=(sequence++%32)+1;
            jitter=new Vector2(Halton(phase,2)-.5f,Halton(phase,3)-.5f);
            var projection=unjittered;
            projection.m02+=2*jitter.x/iw; projection.m12+=2*jitter.y/ih;
            camera.projectionMatrix=projection; projectionOwned=true;
            if(Vector3.Distance(lastPosition,camera.transform.position)>5 || Quaternion.Angle(lastRotation,camera.transform.rotation)>45 ||
               Math.Abs(lastFov-camera.fieldOfView)>1 || Time.realtimeSinceStartup-lastRenderTime>.25f) reset=true;
            lastPosition=camera.transform.position; lastRotation=camera.transform.rotation; lastFov=camera.fieldOfView;
        }
        catch(Exception ex) { Fail(ex.Message); }
    }
    private static bool RenderImage(PostProcessLayer __instance,RenderTexture src,RenderTexture dst)
    {
        if(camera==null || __instance!=layer || output==null || colorCopy==null || depthCopy==null || motionCopy==null || Plugin.Instance.RenderMode.Value==DlssMode.Off) return true;
        try
        {
            if(src==null || colorPtr==IntPtr.Zero || depthPtr==IntPtr.Zero || motionPtr==IntPtr.Zero || outputPtr==IntPtr.Zero)
                throw new InvalidOperationException("Same-camera owned color/depth/motion buffers have no native resources");
            if(src.width!=iw || src.height!=ih)
                throw new InvalidOperationException($"Engine source {src.name} is {src.width}x{src.height}, requested {iw}x{ih}; camera pixels={camera.pixelWidth}x{camera.pixelHeight}, target={camera.targetTexture?.name}; refusing to downsample a full-resolution render and claim an optimization");
            Graphics.Blit(src,colorCopy);
            var previous=ReadStatus();
            if(sequence>3 && previous.result!=0 && previous.result!=1) throw new InvalidOperationException(previous.message);
            Frame frame=new()
            {
                size=96,version=1,color=colorPtr,depth=depthPtr,motion=motionPtr,output=outputPtr,
                inputWidth=(uint)iw,inputHeight=(uint)ih,outputWidth=(uint)ow,outputHeight=(uint)oh,
                quality=appliedMode==DlssMode.Quality?2:appliedMode==DlssMode.Balanced?1:appliedMode==DlssMode.Performance?0:5,
                reset=reset?1:0,reversedDepth=SystemInfo.usesReversedZBuffer?1:0,
                // Color reaches this hook after the existing tone mapping/post stack.
                hdr=0,jitterX=jitter.x,jitterY=-jitter.y,motionScaleX=-iw,motionScaleY=-ih,deltaMs=Time.unscaledDeltaTime*1000
            };
            IntPtr packet=NS_CreatePacket(ref frame);
            if(packet==IntPtr.Zero) throw new InvalidOperationException("Native DLSS packet rejected");
            var cmd=new CommandBuffer{name="NightShift DLSS evaluation"};
            try { cmd.IssuePluginEventAndData(renderEvent,1,packet); Graphics.ExecuteCommandBuffer(cmd); }
            finally { cmd.Release(); }
            // Match the original OnRenderImage contract, including non-null targets
            // used by later image effects. Never replace the main camera's target.
            if(sceneTarget!=null && presentBuffer!=null)
            {
                // Satisfy the scene camera's original image-effect destination;
                // its render target remains consistent until rendering finishes.
                if(src!=dst) Graphics.Blit(src,dst);
                presentBuffer.Clear();
                presentBuffer.Blit(previous.frames>activationFrameCount ? output : colorCopy,
                    new RenderTargetIdentifier(BuiltinRenderTextureType.CameraTarget));
            }
            else Graphics.Blit(previous.frames>activationFrameCount ? output : src,dst);
            RenderTexture.active=dst;
            SuccessfulFrames=previous.frames;
            Status=previous.frames>activationFrameCount
                ? $"DLSS {appliedMode} GPU evaluated | {iw}x{ih} -> {ow}x{oh} | visual validation pending"
                : $"DLSS {appliedMode} starting; {iw}x{ih} -> {ow}x{oh}";
            if(!reportedActive && previous.frames>activationFrameCount)
            {
                reportedActive=true;
                Plugin.Instance.Log.LogInfo(Status+$" | post-stack source={src.width}x{src.height} {src.format} | owned depth=RFloat motion=RGHalf");
                var folder=Path.Combine(Paths.BepInExRootPath,"NightShift"); Directory.CreateDirectory(folder);
                File.WriteAllText(Path.Combine(folder,"dlss-status.json"),JsonSerializer.Serialize(new
                {
                    mode=appliedMode.ToString(),camera=camera.name,input=$"{iw}x{ih}",output=$"{ow}x{oh}",
                    nativeEvaluations=previous.frames-activationFrameCount,result=previous.result,colorFormat=colorCopy.format.ToString(),
                    postStackSource=$"{src.width}x{src.height}",bufferCapture="Same-camera BeforeImageEffects command buffer",
                    scene=UnityEngine.SceneManagement.SceneManager.GetActiveScene().name,
                    presentation=sceneTarget==null ? "Original OnRenderImage destination" : "Low-resolution scene target; later empty camera presents to its own full-resolution CameraTarget",
                    validationStatus="unvalidated",note="NGX success alone does not prove visible output, street FPS gain, correct motion vectors or visual parity"
                },new JsonSerializerOptions{WriteIndented=true}));
            }
            reset=false; lastRenderTime=Time.realtimeSinceStartup;
            // No native success after 120 submissions is a failure, not a silent active mode.
            if(sequence>120 && previous.frames==activationFrameCount) throw new InvalidOperationException(previous.message);
            return false;
        }
        catch(Exception ex)
        {
            if(src!=null) Graphics.Blit(src,dst);
            Fail(ex.Message);
            return false;
        }
    }
    private static void AfterRender(PostProcessLayer __instance)
    {
        if(__instance!=layer || camera==null) return;
        if(projectionOwned) { camera.projectionMatrix=unjittered; camera.nonJitteredProjectionMatrix=unjittered; projectionOwned=false; }
    }
    private static void RestoreCameraTarget()
    {
        if(camera!=null && sceneTarget!=null && camera.targetTexture==sceneTarget)
        { camera.targetTexture=null; camera.aspect=originalAspect; }
    }
    private static void LayerDisabled(PostProcessLayer __instance) { if(__instance==layer) Restore(); }
    private static float Halton(int index,int radix)
    {
        float value=0,factor=1;
        while(index>0) { factor/=radix; value+=factor*(index%radix); index/=radix; }
        return value;
    }
    private static (ulong frames,uint result,string message) ReadStatus()
    {
        var text=new StringBuilder(512); NS_GetStatus(text,512,out var count,out var result);
        return(count,result,text.ToString());
    }
    private static void Fail(string reason)
    {
        Restore(); Plugin.Instance.RenderMode.Value=configuredMode=DlssMode.Off;
        Status="DLSS FALLBACK: "+reason;
        Plugin.Instance.Log.LogError(Status);
    }
    internal static object Snapshot() => new
    {
        requested=Plugin.Instance.RenderMode.Value.ToString(),active=IsActive,inputResolution=InputResolution,
        successfulNativeEvaluations=SuccessfulFrames,status=Status,
        note="Super Resolution/DLAA experimental integration; no frame generation. Runtime success does not establish correct moving-object motion vectors, UI separation or visual parity."
    };
    internal static void CaptureOwnedTextures(string stem)
    {
        var saved=RenderTexture.active;
        try
        {
            foreach(var item in new[]{(name:"input",texture:colorCopy),(name:"output",texture:output)})
            {
                if(item.texture==null) continue;
                RenderTexture.active=item.texture;
                var texture=new Texture2D(item.texture.width,item.texture.height,TextureFormat.RGBA32,false);
                try
                {
                    texture.ReadPixels(new Rect(0,0,item.texture.width,item.texture.height),0,0,false);
                    texture.Apply(false,false);
                    File.WriteAllBytes(stem+"-"+item.name+".png",ImageConversion.EncodeToPNG(texture).ToArray());
                }
                finally { Object.Destroy(texture); }
            }
        }
        finally { RenderTexture.active=saved; }
    }
    internal static void Restore()
    {
        // Unity owns device teardown. Do not enqueue graphics work or touch
        // destroyed camera objects once application shutdown has begun.
        if(quitting) return;
        FrameGenRenderer.ResetRendering();
        if(camera!=null)
        {
            if(captureBuffers!=null) camera.RemoveCommandBuffer(CameraEvent.BeforeImageEffects,captureBuffers);
            RestoreCameraTarget();
            camera.allowMSAA=oldMsaa; camera.depthTextureMode=oldDepth;
            camera.useJitteredProjectionMatrixForTransparentRendering=oldTransparentJitter;
            if(projectionOwned) { camera.projectionMatrix=unjittered; camera.nonJitteredProjectionMatrix=unjittered; }
        }
        if(layer!=null) { layer.antialiasingMode=oldAa; layer.finalBlitToCameraTarget=oldFinalBlit; layer.ResetHistory(); }
        camera=null; layer=null; projectionOwned=false;
        if(presentation!=null)
        {
            presentation.enabled=false;
            if(presentBuffer!=null) presentation.RemoveCommandBuffer(CameraEvent.AfterEverything,presentBuffer);
        }
        presentBuffer?.Release(); presentBuffer=null;
        if(presentationObject!=null) Object.Destroy(presentationObject);
        presentation=null; presentationObject=null;
        captureBuffers?.Release(); captureBuffers=null;
        foreach(var rt in new[]{sceneTarget,output,colorCopy,depthCopy,motionCopy}) if(rt!=null) { rt.Release(); Object.Destroy(rt); }
        sceneTarget=output=colorCopy=depthCopy=motionCopy=null;
        colorPtr=depthPtr=motionPtr=outputPtr=IntPtr.Zero;
        if(available)
        {
            var cmd=new CommandBuffer{name="NightShift DLSS release"};
            try { cmd.IssuePluginEventAndData(renderEvent,2,IntPtr.Zero); Graphics.ExecuteCommandBuffer(cmd); }
            finally { cmd.Release(); }
        }
    }
    internal static void Uninstall() { Restore(); renderHooks?.UnpatchSelf(); renderHooks=null; }
    internal static void MarkQuitting() { quitting=true; }
}
