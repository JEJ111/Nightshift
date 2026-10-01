using System.Text.Json;
using System.Text.RegularExpressions;
using System.Diagnostics;
using BepInEx;
using UnityEngine;

namespace NightShift;

// Optional development-only commands, handled on Unity's main thread.
// No game input automation, gameplay calls, process shutdown or save changes.
internal static class RenderValidation
{
    private sealed class Request
    {
        public string id {get;set;}="";
        public string mode {get;set;}="";
        public string frameGeneration {get;set;}="";
        public bool screenshot {get;set;}
        public float delaySeconds {get;set;}=3;
        public bool benchmark {get;set;}
        public int benchmarkSeconds {get;set;}=20;
    }
    private sealed class Measurement
    {
        public string id="",scene="";
        public DlssMode mode;
        public List<double> intervals=new();
        public long start,last;
        public int seconds,characters;
        public bool valid=true;
        public Vector3 position;
        public Quaternion rotation;
    }
    private static float nextPoll, captureAt;
    private static string lastId="";
    private static Request? pending;
    private static Measurement? measuring;
    private static string Root => Path.Combine(Paths.BepInExRootPath,"NightShift","render-validation");
    internal static void Update()
    {
        if(!Plugin.Instance.DevelopmentControl.Value) return;
        float now=Time.realtimeSinceStartup;
        if(measuring!=null)
        {
            var sample=measuring;
            long tick=Stopwatch.GetTimestamp();
            sample.intervals.Add((tick-sample.last)*1000.0/Stopwatch.Frequency); sample.last=tick;
            var main=Camera.main;
            if(!Application.isFocused || Plugin.Instance.RenderMode.Value!=sample.mode ||
                UnityEngine.SceneManagement.SceneManager.GetActiveScene().name!=sample.scene || main==null ||
                Vector3.Distance(main.transform.position,sample.position)>.05f || Quaternion.Angle(main.transform.rotation,sample.rotation)>.5f)
                sample.valid=false;
            if((tick-sample.start)/(double)Stopwatch.Frequency>=sample.seconds)
            {
                var sorted=sample.intervals.OrderBy(x=>x).ToArray();
                int tail=Math.Max(1,(int)Math.Ceiling(sorted.Length*.01));
                File.WriteAllText(Path.Combine(Root,sample.id+"-benchmark.json"),JsonSerializer.Serialize(new
                {
                    id=sample.id,pid=Environment.ProcessId,valid=sample.valid,scene=sample.scene,mode=sample.mode.ToString(),
                    frames=sorted.Length,avgFps=1000/sample.intervals.Average(),p99FrameMs=sorted[(int)Math.Ceiling(sorted.Length*.99)-1],
                    onePercentLowFps=1000/sorted.TakeLast(tail).Average(),startingCharacters=sample.characters,endingCharacters=Registry.Live.Count,
                    resolution=$"{Screen.width}x{Screen.height}",quality=QualitySettings.GetQualityLevel(),
                    vsync=QualitySettings.vSyncCount,frameCap=Application.targetFrameRate,dlss=DlssRenderer.Snapshot(),
                    cameraPosition=new{x=sample.position.x,y=sample.position.y,z=sample.position.z},
                    note="Unity Update intervals; no readbacks during capture. Focus, camera, scene and selected mode checked. Time/weather/crowd changes still require interpretation."
                },new JsonSerializerOptions{WriteIndented=true}));
                Plugin.Instance.Log.LogInfo($"Render benchmark saved: {sample.id}, valid={sample.valid}, FPS={1000/sample.intervals.Average():F1}");
                measuring=null;
            }
        }
        if(pending!=null && now>=captureAt)
        {
            if(pending.benchmark && !Application.isFocused) { captureAt=now+.25f; }
            else
            {
            var request=pending; pending=null;
            Directory.CreateDirectory(Root);
            string stem=Path.Combine(Root,request.id);
            if(request.screenshot)
            {
                DlssRenderer.CaptureOwnedTextures(stem);
                ScreenCapture.CaptureScreenshot(stem+"-final.png");
            }
            WriteStatus(stem+".json",request.id);
            Plugin.Instance.Log.LogInfo("Render validation captured: "+stem);
            if(request.benchmark)
            {
                var main=Camera.main;
                if(main!=null)
                {
                    long tick=Stopwatch.GetTimestamp();
                    measuring=new Measurement{id=request.id,scene=UnityEngine.SceneManagement.SceneManager.GetActiveScene().name,
                        mode=Plugin.Instance.RenderMode.Value,start=tick,last=tick,seconds=Math.Clamp(request.benchmarkSeconds,5,60),
                        characters=Registry.Live.Count,position=main.transform.position,rotation=main.transform.rotation};
                    Plugin.Instance.Log.LogInfo("Render benchmark recording: "+request.id);
                }
            }
            }
        }
        if(now<nextPoll) return;
        nextPoll=now+1;
        Directory.CreateDirectory(Root);
        WriteStatus(Path.Combine(Root,"live.json"),lastId);
        string file=Path.Combine(Paths.BepInExRootPath,"NightShift","render-test-request.json");
        if(!File.Exists(file) || new FileInfo(file).Length>4096) return;
        var requested=JsonSerializer.Deserialize<Request>(File.ReadAllText(file));
        if(requested==null || requested.id==lastId || !Regex.IsMatch(requested.id,"^[A-Za-z0-9_-]{1,80}$")) return;
        if(requested.benchmark && requested.screenshot) throw new InvalidOperationException("Readback and benchmark must use separate requests");
        if(requested.mode.Length!=0)
        {
            if(!Enum.TryParse<DlssMode>(requested.mode,true,out var mode) || !Enum.IsDefined(typeof(DlssMode),mode))
                throw new InvalidOperationException("Invalid render validation mode");
            DlssRenderer.Choose(mode);
        }
        if(requested.frameGeneration.Length!=0)
        {
            if(!Enum.TryParse<FrameGenMode>(requested.frameGeneration,true,out var mode) || !Enum.IsDefined(typeof(FrameGenMode),mode))
                throw new InvalidOperationException("Invalid frame-generation mode");
            FrameGenRenderer.Choose(mode);
        }
        lastId=requested.id; pending=requested;
        captureAt=now+Math.Clamp(requested.delaySeconds,1,30);
        Plugin.Instance.Log.LogInfo("Render validation request accepted: "+lastId);
    }
    private static void WriteStatus(string path,string id)
    {
        var main=Camera.main;
        File.WriteAllText(path,JsonSerializer.Serialize(new
        {
            id,pid=Environment.ProcessId,utc=DateTime.UtcNow,unityTime=Time.realtimeSinceStartup,
            focused=Application.isFocused,scene=UnityEngine.SceneManagement.SceneManager.GetActiveScene().name,
            resolution=$"{Screen.width}x{Screen.height}",characters=Registry.Live.Count,
            dlss=DlssRenderer.Snapshot(),frameGeneration=FrameGenRenderer.Snapshot(),
            camera=main==null?null:new {name=main.name,pixels=$"{main.pixelWidth}x{main.pixelHeight}",
                target=main.targetTexture==null?"screen":main.targetTexture.name,aspect=main.aspect,
                rect=new {x=main.rect.x,y=main.rect.y,width=main.rect.width,height=main.rect.height}},
            note="Development readback can stall rendering; exclude captures from performance comparisons"
        },new JsonSerializerOptions{WriteIndented=true}));
    }
}
