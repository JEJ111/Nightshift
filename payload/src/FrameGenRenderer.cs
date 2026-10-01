using System.Runtime.InteropServices;
using System.Text;
using BepInEx;
using HarmonyLib;
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using Nivalis;
using UnityEngine;
using UnityEngine.LowLevel;
using UnityEngine.Rendering;
using UnityEngine.Rendering.PostProcessing;
using Object = UnityEngine.Object;

namespace NightShift;

public enum FrameGenMode { Off, On2x, On3x, On4x }

internal static class FrameGenRenderer
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Matrix
    {
        public float a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p;
        internal static Matrix RowLayout(Matrix4x4 column) => new()
        {
            a=column.m00,b=column.m10,c=column.m20,d=column.m30,
            e=column.m01,f=column.m11,g=column.m21,h=column.m31,
            i=column.m02,j=column.m12,k=column.m22,l=column.m32,
            m=column.m03,n=column.m13,o=column.m23,p=column.m33
        };
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct Frame
    {
        public uint size,version,frameId,flags;
        public IntPtr hudless,depth,motion;
        public uint inputWidth,inputHeight,outputWidth,outputHeight;
        public float jitterX,jitterY,motionScaleX,motionScaleY,depthNear,depthFar,fov,aspect;
        public float px,py,pz,ux,uy,uz,rx,ry,rz,fx,fy,fz;
        public Matrix viewToClip,clipToView,clipToPrevious,previousToClip;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct NativeStatus
    {
        public uint size,version,ready,requested,active,apiResult,fgStatus,format;
        public ulong rendered,presented,generated,submitted;
        public double renderedFps,presentedFps;
    }
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void FrameCall(uint id);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetEvent();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr CreatePacket(ref Frame frame);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void DiscardPacket(IntPtr packet);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void GetStatus(ref NativeStatus status,StringBuilder message,uint capacity);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint MaximumCall();
    private static MaximumCall? maximum;
    private static FrameCall? begin,end,configure;
    private static CreatePacket? createPacket;
    private static DiscardPacket? discardPacket;
    private static GetStatus? getStatus;
    private static PlayerLoopSystem.UpdateFunction? beginLoop,endLoop;
    private static readonly Action beginAction=SimulationStart,endAction=SimulationEnd;
    private static Harmony? hooks;
    private static IntPtr renderEvent,pendingPacket;
    private static Camera? scene,presentCamera;
    private static CommandBuffer? inputCapture,finalCapture;
    private static RenderTexture? hudless,depth,motion;
    private static DepthTextureMode savedDepth;
    private static int iw,ih,ow,oh,previousFrame=-1;
    private static DlssMode dlssMode;
    private static Matrix4x4 previousViewProjection;
    private static Vector3 previousPosition;
    private static Quaternion previousRotation;
    private static bool available,quitting,loopInstalled;
    private static FrameGenMode configured;
    private static NativeStatus native;
    private static string nativeMessage="Frame generation unavailable: early DXGI bridge not installed";
    private static float nextStatus;
    private static ulong beginCallbacks,endCallbacks;
    private static int lastBeginFrame=-1,lastEndFrame=-1;
    internal static uint MaximumMultiplier => available ? maximum?.Invoke()??1u : 1u;
    internal static uint Multiplier(FrameGenMode mode) => mode==FrameGenMode.Off ? 0u : (uint)mode+1u;
    internal static FrameGenMode NextMode()
    {
        uint max=Math.Min(4u,MaximumMultiplier);
        uint next=configured==FrameGenMode.Off?2u:Multiplier(configured)+1u;
        return next<=max?(FrameGenMode)(next-1u):FrameGenMode.Off;
    }
    internal static string MenuStatus => !available ? "Frame generation: Unavailable"
        : configured==FrameGenMode.Off ? "Frame generation: Off"
        : $"Frame generation: {Multiplier(configured)}x" + (native.active!=0?" (active)":" (waiting for gameplay)");
    internal static string PerformanceStatus(double fallback) => native.active!=0
        ? $"Rendered: {native.renderedFps:F0} FPS   |   SDK presented: {native.presentedFps:F0} FPS"
        : $"Rendered: {fallback:F0} FPS";
    internal static string Status => MenuStatus+" | "+nativeMessage+
        $" | rendered {native.renderedFps:F1} FPS | SDK presented {native.presentedFps:F1} FPS | generated {native.generated}";
    private static T Export<T>(IntPtr library,string name) where T:Delegate =>
        Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(library,name));

    internal static void Install()
    {
        if(Marshal.SizeOf<Frame>()!=392 || Marshal.SizeOf<NativeStatus>()!=80)
            throw new InvalidOperationException("Frame-generation ABI mismatch");
        if(SystemInfo.graphicsDeviceType!=GraphicsDeviceType.Direct3D11) return;
        string proxy=Path.Combine(Paths.GameRootPath,"dxgi.dll");
        if(!File.Exists(proxy)) return;
        var library=NativeLibrary.Load(proxy);
        if(!NativeLibrary.TryGetExport(library,"NSFG_GetStatus",out _)) return;
        begin=Export<FrameCall>(library,"NSFG_BeginFrame"); end=Export<FrameCall>(library,"NSFG_EndSimulation");
        maximum=Export<MaximumCall>(library,"NSFG_GetMaximumMultiplier");
        configure=Export<FrameCall>(library,"NSFG_Configure"); createPacket=Export<CreatePacket>(library,"NSFG_CreatePacket");
        discardPacket=Export<DiscardPacket>(library,"NSFG_DiscardPacket"); getStatus=Export<GetStatus>(library,"NSFG_GetStatus");
        renderEvent=Export<GetEvent>(library,"NSFG_GetRenderEvent")();
        RefreshStatus(); available=native.ready!=0 && renderEvent!=IntPtr.Zero;
        if(!available) { Plugin.Instance.Log.LogWarning(nativeMessage); return; }
        configure(0); configured=FrameGenMode.Off;
        beginLoop=(PlayerLoopSystem.UpdateFunction)beginAction; endLoop=(PlayerLoopSystem.UpdateFunction)endAction;
        var root=PlayerLoop.GetCurrentPlayerLoop();
        if(!Insert(root,"UnityEngine.PlayerLoop.EarlyUpdate",beginLoop) || !Insert(root,"UnityEngine.PlayerLoop.PostLateUpdate",endLoop))
            throw new InvalidOperationException("Reflex player-loop phases were not found");
        PlayerLoop.SetPlayerLoop(root); loopInstalled=true;
        hooks=new Harmony(Plugin.Id+".framegen");
        hooks.Patch(AccessTools.DeclaredMethod(typeof(PostProcessLayer),"OnPreCull"),
            postfix:new HarmonyMethod(typeof(FrameGenRenderer),nameof(AfterCull)){priority=Priority.Last});
        Plugin.Instance.Log.LogInfo("NVIDIA FG bridge ready. F11 cycles supported 2x/3x/4x modes; always starts OFF. Reflex begins before input and completes simulation before rendering.");
    }
    private static bool Insert(PlayerLoopSystem node,string phase,PlayerLoopSystem.UpdateFunction callback)
    {
        if(node.type?.FullName==phase)
        {
            var old=node.subSystemList;
            var list=new PlayerLoopSystem[(old?.Length??0)+1];
            list[0]=new PlayerLoopSystem{type=node.type,updateDelegate=callback};
            for(int i=1;i<list.Length;i++) list[i]=old![i-1];
            node.subSystemList=new Il2CppReferenceArray<PlayerLoopSystem>(list); return true;
        }
        if(node.subSystemList!=null)
        {
            var list=node.subSystemList;
            for(int i=0;i<list.Length;i++)
            {
                var child=list[i];
                if(!Insert(child,phase,callback)) continue;
                // These IL2CPP value types are exposed as boxed wrappers. Write
                // the edited value back into its native array at every level.
                list[i]=child; node.subSystemList=list; return true;
            }
        }
        return false;
    }
    private static void Remove(PlayerLoopSystem node)
    {
        if(node.subSystemList==null) return;
        var list=new List<PlayerLoopSystem>();
        foreach(var child in node.subSystemList)
        {
            var callback=child.updateDelegate;
            if(callback!=null && (callback.Pointer==beginLoop?.Pointer || callback.Pointer==endLoop?.Pointer)) continue;
            Remove(child); list.Add(child);
        }
        node.subSystemList=new Il2CppReferenceArray<PlayerLoopSystem>(list.ToArray());
    }
    private static void SimulationStart()
    {
        if(quitting || !available) return;
        try {
            lastBeginFrame=Time.frameCount; begin!((uint)lastBeginFrame); ++beginCallbacks;
            if(beginCallbacks==1) Plugin.Instance.Log.LogInfo("Reflex EarlyUpdate callback running at frame "+lastBeginFrame);
        }
        catch(Exception error) { Fail("Reflex begin: "+error.Message); }
    }
    private static void SimulationEnd()
    {
        if(quitting || !available) return;
        try {
            lastEndFrame=Time.frameCount; end!((uint)lastEndFrame); ++endCallbacks;
            if(endCallbacks==1) Plugin.Instance.Log.LogInfo("Reflex PostLateUpdate callback running at frame "+lastEndFrame);
        }
        catch(Exception error) { Fail("Reflex end: "+error.Message); }
    }
    internal static void Choose(FrameGenMode mode)
    {
        if(mode!=FrameGenMode.Off && !available)
        { Plugin.Instance.Log.LogWarning(nativeMessage); Plugin.Instance.FrameGeneration.Value=FrameGenMode.Off; return; }
        if(!Enum.IsDefined(typeof(FrameGenMode),mode) || Multiplier(mode)>MaximumMultiplier) mode=FrameGenMode.Off;
        ResetRendering(); configured=mode; Plugin.Instance.FrameGeneration.Value=mode;
        configure?.Invoke(Multiplier(mode));
        Plugin.Instance.Log.LogInfo("Frame generation "+mode+" selected");
    }
    internal static void Update()
    {
        if(!available || quitting) return;
        if(Plugin.Instance.FrameGeneration.Value!=configured) Choose(Plugin.Instance.FrameGeneration.Value);
        if(Time.realtimeSinceStartup>=nextStatus)
        {
            nextStatus=Time.realtimeSinceStartup+.5f; RefreshStatus();
            if(configured!=FrameGenMode.Off && native.requested==0 && native.apiResult!=0)
                Fail(nativeMessage);
        }
    }
    private static void RefreshStatus()
    {
        if(getStatus==null) return;
        native=new NativeStatus{size=80}; var text=new StringBuilder(1024);
        getStatus(ref native,text,1024); nativeMessage=text.ToString();
    }
    private static RenderTexture Texture(int width,int height,RenderTextureFormat format,RenderTextureReadWrite color,string name)
    {
        var rt=new RenderTexture(width,height,0,format,color){name=name,antiAliasing=1,useMipMap=false};
        if(!rt.Create()) { Object.Destroy(rt); throw new InvalidOperationException("Could not allocate "+name); }
        return rt;
    }
    internal static void PrepareFrame()
    {
        if(!available || quitting) return;
        var main=Camera.main;
        var target=DlssRenderer.PresentationCamera??main;
        int inputWidth=DlssRenderer.SceneInputWidth,inputHeight=DlssRenderer.SceneInputHeight;
        if(scene!=main || presentCamera!=target || ow!=Screen.width || oh!=Screen.height ||
            iw!=inputWidth || ih!=inputHeight || dlssMode!=Plugin.Instance.RenderMode.Value) ResetRendering();
        if(main==null || target==null) return;
        if(scene==null)
        {
            scene=main; presentCamera=target; savedDepth=main.depthTextureMode;
            dlssMode=Plugin.Instance.RenderMode.Value; iw=inputWidth; ih=inputHeight; ow=Screen.width; oh=Screen.height;
            finalCapture=new CommandBuffer{name="NightShift FG camera frame/presentation capture"};
            presentCamera.AddCommandBuffer(CameraEvent.AfterEverything,finalCapture);
            if(configured!=FrameGenMode.Off)
            {
                var format=native.format==87?RenderTextureFormat.BGRA32:RenderTextureFormat.ARGB32;
                hudless=Texture(ow,oh,format,RenderTextureReadWrite.sRGB,"NightShift FG pre-overlay scene");
                depth=Texture(iw,ih,RenderTextureFormat.RFloat,RenderTextureReadWrite.Linear,"NightShift FG depth");
                motion=Texture(iw,ih,RenderTextureFormat.RGHalf,RenderTextureReadWrite.Linear,"NightShift FG motion");
                main.depthTextureMode|=DepthTextureMode.Depth|DepthTextureMode.MotionVectors;
                inputCapture=new CommandBuffer{name="NightShift FG same-camera inputs"};
                inputCapture.Blit(new RenderTargetIdentifier(BuiltinRenderTextureType.Depth),new RenderTargetIdentifier(depth));
                inputCapture.Blit(new RenderTargetIdentifier(BuiltinRenderTextureType.MotionVectors),new RenderTargetIdentifier(motion));
                main.AddCommandBuffer(CameraEvent.BeforeImageEffects,inputCapture);
                Plugin.Instance.Log.LogInfo($"FG buffers: {iw}x{ih} depth/motion; {ow}x{oh} scene/UI; camera={main.name}, present={target.name}");
            }
        }
        finalCapture!.Clear(); CancelPacket();
        finalCapture.IssuePluginEventAndData(renderEvent,3,new IntPtr(Time.frameCount));
    }
    internal static bool IsGameplayReady()
    {
        if(!Application.isFocused || Time.timeScale<=0) return false;
        if(!Singleton<GameSceneManager>.InstanceExist()) return false;
        var manager=Singleton<GameSceneManager>._instance;
        return manager!=null && manager.IsGame && manager.IsGameplayInitialized && !manager.IsLoading && !GameSceneManager.IsUnloadingGameplay;
    }
    private static bool ValidGameplay()
    {
        if(!Application.isFocused || Time.timeScale<=0 || scene==null || scene.orthographic || scene.stereoEnabled) return false;
        // Read the existing singleton only; never create a manager for this mod.
        if(!Singleton<GameSceneManager>.InstanceExist()) return false;
        var manager=Singleton<GameSceneManager>._instance;
        return manager!=null && manager.IsGame && manager.IsGameplayInitialized && !manager.IsLoading && !GameSceneManager.IsUnloadingGameplay;
    }
    private static void AfterCull(PostProcessLayer __instance)
    {
        if(quitting || !available || __instance.m_Camera!=scene || finalCapture==null) return;
        try
        {
            finalCapture.Clear(); CancelPacket();
            uint frameId=(uint)Time.frameCount;
            finalCapture.IssuePluginEventAndData(renderEvent,3,new IntPtr(frameId));
            if(configured==FrameGenMode.Off || !ValidGameplay() || hudless==null || depth==null || motion==null) { previousFrame=-1; return; }
            var main=scene!;
            var projection=GL.GetGPUProjectionMatrix(DlssRenderer.UnjitteredProjection??main.nonJitteredProjectionMatrix,true);
            var view=main.worldToCameraMatrix;
            var vp=projection*view;
            bool reset=previousFrame!=Time.frameCount-1 || Vector3.Distance(main.transform.position,previousPosition)>5 || Quaternion.Angle(main.transform.rotation,previousRotation)>45;
            var toPrevious=reset?Matrix4x4.identity:previousViewProjection*vp.inverse;
            var jitter=DlssRenderer.CameraJitter??new Vector2((main.projectionMatrix.m02-main.nonJitteredProjectionMatrix.m02)*iw*.5f,(main.projectionMatrix.m12-main.nonJitteredProjectionMatrix.m12)*ih*.5f);
            var pos=main.transform.position; var up=main.transform.up; var right=main.transform.right; var forward=main.transform.forward;
            Frame frame=new()
            {
                size=392,version=1,frameId=frameId,flags=4u|(SystemInfo.usesReversedZBuffer?1u:0u)|(reset?2u:0u),
                hudless=hudless.GetNativeTexturePtr(),depth=depth.GetNativeTexturePtr(),motion=motion.GetNativeTexturePtr(),
                inputWidth=(uint)iw,inputHeight=(uint)ih,outputWidth=(uint)ow,outputHeight=(uint)oh,
                jitterX=jitter.x,jitterY=-jitter.y,motionScaleX=-1,motionScaleY=-1,
                depthNear=main.nearClipPlane,depthFar=main.farClipPlane,fov=main.fieldOfView*Mathf.Deg2Rad,aspect=main.aspect,
                px=pos.x,py=pos.y,pz=pos.z,ux=up.x,uy=up.y,uz=up.z,rx=right.x,ry=right.y,rz=right.z,fx=forward.x,fy=forward.y,fz=forward.z,
                viewToClip=Matrix.RowLayout(projection),clipToView=Matrix.RowLayout(projection.inverse),
                clipToPrevious=Matrix.RowLayout(toPrevious),previousToClip=Matrix.RowLayout(toPrevious.inverse)
            };
            pendingPacket=createPacket!(ref frame);
            if(pendingPacket==IntPtr.Zero) throw new InvalidOperationException("Native FG packet rejected");
            finalCapture.Blit(new RenderTargetIdentifier(BuiltinRenderTextureType.CameraTarget),new RenderTargetIdentifier(hudless));
            finalCapture.IssuePluginEventAndData(renderEvent,1,pendingPacket);
            previousFrame=Time.frameCount; previousViewProjection=vp; previousPosition=pos; previousRotation=main.transform.rotation;
        }
        catch(Exception error) { Fail("FG camera capture: "+error.Message); }
    }
    private static void CancelPacket()
    { if(pendingPacket!=IntPtr.Zero) { discardPacket?.Invoke(pendingPacket); pendingPacket=IntPtr.Zero; } }
    internal static void ResetRendering()
    {
        if(quitting) return;
        if(scene!=null)
        {
            if(inputCapture!=null) scene.RemoveCommandBuffer(CameraEvent.BeforeImageEffects,inputCapture);
            if(inputCapture!=null) scene.depthTextureMode=savedDepth;
        }
        if(presentCamera!=null && finalCapture!=null) presentCamera.RemoveCommandBuffer(CameraEvent.AfterEverything,finalCapture);
        inputCapture?.Release(); finalCapture?.Release(); inputCapture=finalCapture=null;
        CancelPacket();
        foreach(var rt in new[]{hudless,depth,motion}) if(rt!=null) { rt.Release(); Object.Destroy(rt); }
        hudless=depth=motion=null; scene=presentCamera=null; previousFrame=-1;
    }
    private static void Fail(string reason)
    {
        Choose(FrameGenMode.Off); nativeMessage="Frame generation disabled: "+reason;
        Plugin.Instance.Log.LogError(nativeMessage);
    }
    internal static object Snapshot() => new
    {
        requested=configured.ToString(),available,active=native.active!=0,apiResult=native.apiResult,fgStatus=native.fgStatus,
        renderedFrames=native.rendered,sdkPresentedFrames=native.presented,generatedFrames=native.generated,submittedFrames=native.submitted,
        renderedFps=native.renderedFps,sdkPresentedFps=native.presentedFps,
        beginCallbacks,endCallbacks,lastBeginFrame,lastEndFrame,
        input=$"{iw}x{ih}",output=$"{ow}x{oh}",lastCameraFrame=previousFrame,nativeMessage,
        note="SDK presents include generated frames; physical display pacing and moving-object/UI quality need player validation."
    };
    internal static void MarkQuitting() { configure?.Invoke(0); quitting=true; }
    internal static void Uninstall()
    {
        configure?.Invoke(0); ResetRendering(); hooks?.UnpatchSelf(); hooks=null;
        if(loopInstalled && !quitting) { var root=PlayerLoop.GetCurrentPlayerLoop(); Remove(root); PlayerLoop.SetPlayerLoop(root); }
        loopInstalled=false; available=false;
    }
}
