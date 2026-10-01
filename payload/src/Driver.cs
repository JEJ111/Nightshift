using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using BepInEx;
using UnityEngine;
using Nivalis;
using UnityEngine.InputSystem;
using Object = UnityEngine.Object;

namespace NightShift;

public sealed class Driver : MonoBehaviour
{
    internal static Driver? Active;
    private readonly Queue<int> pending = new();
    private readonly HashSet<int> queued = new();
    private readonly Dictionary<int, List<Renderer>> changed = new();
    private readonly double[] frameRing = new double[120];
    private int ringCursor, ringCount;
    private long lastFrame;
    private readonly List<double> capture = new(10000);
    private readonly List<double> crowdCapture = new(10000);
    private readonly List<double> animationCapture = new(10000);
    private readonly List<int> animationCalls = new(10000);
    private bool diagnosticCapture;
    private DlssMode captureDlssMode;
    private string captureId = "";
    private long captureStart;
    private bool captureMode, captureInProgress;
    private string captureScene = "";
    private bool invalidCapture;
    private bool lastMode;
    private bool guiSeen;
    private double displayedFps;
    private float nextHudRefresh;
    private float nextConfigCheck;
    private DateTime configModified;
    private string status = "F9: record 30 seconds in a stationary crowded view";
    public Driver(IntPtr pointer) : base(pointer) { }

    public void Awake()
    {
        Active = this;
        lastMode = Plugin.Instance.Optimize.Value;
        Registry.Releasing += Restore;
        configModified=File.GetLastWriteTimeUtc(Plugin.Instance.Config.ConfigFilePath);
    }
    internal void Queue(int id) { if (queued.Add(id)) pending.Enqueue(id); }
    public void Update()
    {
        try
        {
            if(Time.realtimeSinceStartup>=nextConfigCheck)
            {
                nextConfigCheck=Time.realtimeSinceStartup+2;
                var modified=File.GetLastWriteTimeUtc(Plugin.Instance.Config.ConfigFilePath);
                if(modified!=configModified)
                {
                    Plugin.Instance.Config.Reload(); configModified=File.GetLastWriteTimeUtc(Plugin.Instance.Config.ConfigFilePath);
                    Plugin.Instance.Log.LogInfo("NightShift configuration reloaded.");
                }
            }
            DlssRenderer.BeginFrame();
            FrameGenRenderer.Update();
            RenderValidation.Update();
            long now = Stopwatch.GetTimestamp();
            if (lastFrame != 0)
            {
                double ms = (now - lastFrame) * 1000.0 / Stopwatch.Frequency;
                frameRing[ringCursor++ % frameRing.Length] = ms;
                ringCount = Math.Min(ringCount + 1, frameRing.Length);
                if (captureInProgress)
                {
                    capture.Add(ms); crowdCapture.Add(CrowdTiming.UpdateMs + CrowdTiming.LateMs);
                    if (diagnosticCapture)
                    {
                        animationCapture.Add(DiagnosticTiming.UpdateAnimationMs+DiagnosticTiming.LateAnimationMs);
                        animationCalls.Add(DiagnosticTiming.UpdateAnimationCalls+DiagnosticTiming.LateAnimationCalls);
                        DiagnosticTiming.SampleMarkers();
                    }
                }
            }
            lastFrame = now;
            var keyboard = Application.isFocused ? Keyboard.current : null;
            if (keyboard != null && keyboard.f7Key.wasPressedThisFrame) Plugin.Instance.Hud.Value = !Plugin.Instance.Hud.Value;
            if (keyboard != null && keyboard.f5Key.wasPressedThisFrame && !captureInProgress)
                DlssRenderer.Choose(Plugin.Instance.RenderMode.Value==DlssMode.DLAA?DlssMode.Off:DlssMode.DLAA);
            if (keyboard != null && keyboard.f11Key.wasPressedThisFrame && !captureInProgress)
                FrameGenRenderer.Choose(FrameGenRenderer.NextMode());
            bool mode = Plugin.Instance.Optimize.Value;
            if (mode != lastMode)
            {
                if (captureInProgress) { invalidCapture = true; FinishCapture(); }
                RestoreAll();
                lastMode = mode;
                if (mode) foreach (int id in Registry.Live.Keys) Queue(id);
                Plugin.Instance.Log.LogInfo($"Renderer experiment {(mode ? "enabled" : "disabled")}. NPC density and gameplay unchanged.");
                Plugin.Instance.Hud.Value = true;
                status = mode ? "Experiment ENABLED; inspecting eligible meshes..." : "Experiment OFF; renderer changes restored";
            }
            // Bound discovery work to at most one character per frame.
            // Unity API calls cannot be interrupted mid-call.
            if (mode && pending.Count > 0)
            {
                int id = pending.Dequeue(); queued.Remove(id);
                if (Registry.Live.TryGetValue(id, out var character) && character != null) Optimize(character,id);
                if (pending.Count == 0 && !captureInProgress)
                    status = $"Experiment ENABLED, {ChangedCount} meshes changed" + (ChangedCount == 0 ? " (no eligible meshes found)" : "");
            }
            if (captureInProgress)
            {
                if (!Application.isFocused) invalidCapture = true;
                if(Plugin.Instance.RenderMode.Value!=captureDlssMode) invalidCapture=true;
                if (UnityEngine.SceneManagement.SceneManager.GetActiveScene().name != captureScene) invalidCapture = true;
                status = $"RECORDING {(diagnosticCapture ? "deep profile" : captureMode ? "experiment" : "baseline")} | {Math.Max(0,Math.Clamp(Plugin.Instance.CaptureSeconds.Value,5,120)-(now-captureStart)/(double)Stopwatch.Frequency):F0}s left | keep camera still";
                if ((now - captureStart) / (double)Stopwatch.Frequency >= Math.Clamp(Plugin.Instance.CaptureSeconds.Value,5,120)) FinishCapture();
            }
            if (Time.realtimeSinceStartup >= nextHudRefresh)
            {
                nextHudRefresh = Time.realtimeSinceStartup + 0.5f;
                double total = 0; for (int i = 0; i < ringCount; i++) total += frameRing[i];
                displayedFps = total > 0 ? ringCount * 1000.0 / total : 0;
            }
        }
        catch (Exception ex)
        {
            Plugin.Instance.Log.LogError($"Experiment disabled after an error: {ex}");
            Plugin.Instance.Optimize.Value = false;
            if (captureInProgress) { invalidCapture = true; captureInProgress = false; }
            DiagnosticTiming.End();
            status = "Capture stopped after an error; see BepInEx/LogOutput.log";
            RestoreAll();
        }
    }
    private int ChangedCount => changed.Values.Sum(v => v.Count);
    public void LateUpdate()
    {
        try { DlssRenderer.PrepareFrame(); }
        catch(Exception ex) { Plugin.Instance.Log.LogError("DLSS frame setup: "+ex); DlssRenderer.Choose(DlssMode.Off); }
        try { FrameGenRenderer.PrepareFrame(); }
        catch(Exception ex) { Plugin.Instance.Log.LogError("FG frame setup: "+ex); FrameGenRenderer.Choose(FrameGenMode.Off); }
    }
    private void Optimize(Character c,int id)
    {
        // Never override a functioning engine LODGroup (renderer.enabled alone
        // does not prove that Unity is drawing all LODs).
        var lods = c.GetComponentsInChildren<LODGroup>(true);
        foreach (var g in lods) if (g != null && g.enabled && g.gameObject.activeInHierarchy) return;
        var renderers = c.GetComponentsInChildren<Renderer>(true);
        var candidates = new List<LodCandidate>(renderers.Length);
        foreach (var r in renderers)
        {
            if (r == null) continue;
            var parent = r.transform.parent;
            var ancestor = r.GetComponentInParent<LODGroup>();
            if (ancestor != null && ancestor.enabled && ancestor.gameObject.activeInHierarchy) return;
            // Shadows may be visible in other districts/times of day. Preserve all
            // shadow-only meshes, even if their names look like LOD candidates.
            bool skinned = r.TryCast<SkinnedMeshRenderer>() != null &&
                r.shadowCastingMode != UnityEngine.Rendering.ShadowCastingMode.ShadowsOnly;
            candidates.Add(new(r.GetInstanceID(), parent == null ? 0 : parent.GetInstanceID(),r.name,r.enabled,r.gameObject.activeInHierarchy,skinned));
        }
        var hide = LodPolicy.Select(candidates);
        if (hide.Count == 0) return;
        if (!changed.TryGetValue(id,out var list)) changed[id] = list = new();
        foreach (var r in renderers)
        {
            if (r != null && r.enabled && hide.Contains(r.GetInstanceID()))
            {
                list.Add(r); // Record ownership before changing the native object.
                r.enabled = false;
            }
        }
    }
    internal void Restore(int id)
    {
        queued.Remove(id);
        if (!changed.Remove(id,out var list)) return;
        foreach (var r in list)
        {
            try { if (r != null && !r.enabled) r.enabled = true; }
            catch (Exception ex) { Plugin.Instance.Log.LogWarning($"Renderer restore: {ex.Message}"); }
        }
    }
    private void RestoreAll()
    {
        foreach (int id in changed.Keys.ToArray()) Restore(id);
        pending.Clear(); queued.Clear();
    }
    private void FinishCapture()
    {
        captureInProgress = false;
        if (capture.Count == 0) { DiagnosticTiming.End(); return; }
        var sorted = capture.OrderBy(ms => ms).ToArray();
        double p99 = sorted[(int)Math.Ceiling(sorted.Length * 0.99) - 1];
        var directory = Path.Combine(Paths.BepInExRootPath,"NightShift","captures");
        Directory.CreateDirectory(directory);
        string stem = captureId + (diagnosticCapture ? "-profile" : captureMode ? "-experiment" : "-baseline");
        string csv = Path.Combine(directory,stem + ".csv");
        File.WriteAllLines(csv,new[]{"frame,interval_ms,crowd_update_wall_ms,manual_crowd_animation_ms,manual_animation_calls"}.Concat(capture.Select((ms,i) => $"{i},{ms.ToString("F6",CultureInfo.InvariantCulture)},{crowdCapture[i].ToString("F6",CultureInfo.InvariantCulture)},{(diagnosticCapture ? animationCapture[i].ToString("F6",CultureInfo.InvariantCulture) : "")},{(diagnosticCapture ? animationCalls[i].ToString(CultureInfo.InvariantCulture) : "")}")));
        var summary = new
        {
            valid = !invalidCapture, captureId, mode = diagnosticCapture ? "profile" : captureMode ? "experiment" : "baseline", scene = captureScene,
            frames = capture.Count, avgFps = 1000.0 / capture.Average(), p99FrameMs = p99,
            onePercentLowFps = 1000.0 / sorted.Skip((int)Math.Floor(sorted.Length*0.99)).Average(),
            characters = Registry.Live.Count, changedRenderers = ChangedCount,
            averageCrowdUpdateWallMs = crowdCapture.Average(),
            dlssMode=captureDlssMode.ToString(),dlss=DlssRenderer.Snapshot(),
            averageManualCrowdAnimationMs = diagnosticCapture && animationCalls.Sum() > 0 ? (double?)animationCapture.Average() : null,
            averageManualAnimationCalls = diagnosticCapture ? (double?)animationCalls.Average() : null,
            diagnostics = diagnosticCapture ? DiagnosticTiming.Snapshot() : null,
            resolution = $"{Screen.width}x{Screen.height}", quality = QualitySettings.GetQualityLevel(),
            vsync = QualitySettings.vSyncCount, frameCap = Application.targetFrameRate,
            unity = Application.unityVersion, graphicsApi = SystemInfo.graphicsDeviceType.ToString(),
            gpu = SystemInfo.graphicsDeviceName,
            measurement = "Unity Update callback wall-time intervals, not presented-frame or GPU timings",
            manualRequirements = "Same save, district, location, camera, weather, time, resolution and settings; wait 15 seconds after toggling. No menus, alt-tab, loading or other mods during capture."
        };
        File.WriteAllText(Path.Combine(directory,stem + ".json"),JsonSerializer.Serialize(summary,new JsonSerializerOptions{WriteIndented = true}));
        DiagnosticTiming.End();
        status = $"SAVED {(invalidCapture ? "INVALID" : diagnosticCapture ? "deep profile" : captureMode ? "experiment" : "baseline")}: {summary.avgFps:F1} FPS | BepInEx/NightShift/captures";
        Plugin.Instance.Log.LogInfo(status + " -> " + csv);
    }
    private void Report()
    {
        var path = Path.Combine(Paths.BepInExRootPath,"NightShift","renderer-report.txt");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var lines = new List<string>{"NightShift renderer inventory (enabled does not imply drawn)", $"Characters: {Registry.Live.Count}"};
        // An explicit user action, never a repeating whole-scene diagnostic sweep.
        foreach (var c in Registry.Live.Values.Take(30))
        {
            if (c == null) continue;
            lines.Add($"Character {c.GetInstanceID()} {c.name}");
            foreach (var g in c.GetComponentsInChildren<LODGroup>(true))
                if (g != null) lines.Add($"  LODGroup {g.name} enabled={g.enabled} lodCount={g.lodCount}");
            foreach (var r in c.GetComponentsInChildren<Renderer>(true))
                if (r != null) lines.Add($"  {r.name} enabled={r.enabled} active={r.gameObject.activeInHierarchy} shadow={r.shadowCastingMode}");
        }
        File.WriteAllLines(path,lines);
        status = "Saved renderer-report.txt";
        Plugin.Instance.Log.LogInfo("Renderer report: " + path);
    }
    public void OnGUI()
    {
        try
        {
            if (!guiSeen) { guiSeen = true; Plugin.Instance.Log.LogInfo("Overlay OnGUI callback running."); }
            if (!Plugin.Instance.Hud.Value) return;
            float width = Mathf.Min(520, Screen.width - 24);
            GUI.Box(new Rect(12,12,width,174),"NightShift 0.5.0");
            GUI.Label(new Rect(24,38,width-24,22),FrameGenRenderer.MenuStatus);
            GUI.Label(new Rect(24,63,width-24,22),DlssRenderer.MenuStatus);
            GUI.Label(new Rect(24,88,width-24,22),"DLSS upscaling: Coming soon");
            GUI.Label(new Rect(24,113,width-24,22),FrameGenRenderer.PerformanceStatus(displayedFps));
            GUI.Label(new Rect(24,138,width-24,22),"F11 Frame generation   |   F5 DLAA   |   F7 Hide menu");
        }
        catch (Exception ex)
        {
            Plugin.Instance.Hud.Value = false;
            Plugin.Instance.Log.LogError($"Overlay disabled: {ex}");
        }
    }
    internal void Stop()
    {
        if (captureInProgress) { invalidCapture = true; FinishCapture(); }
        DiagnosticTiming.End();
        RestoreAll(); Registry.Releasing -= Restore;
        Active = null;
    }
    public void OnDestroy() { Stop(); }
    public void OnApplicationQuit() { FrameGenRenderer.MarkQuitting(); DlssRenderer.MarkQuitting(); }
}
