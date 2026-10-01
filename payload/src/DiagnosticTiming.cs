using System.Diagnostics;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Profiling;

namespace NightShift;

// Diagnostic hooks only. No game update is skipped or given a different argument.
internal static class DiagnosticTiming
{
    private static Harmony? hooks;
    internal static bool Active;
    [ThreadStatic] internal static int CrowdDepth;
    [ThreadStatic] private static double routineAnimationMs;
    [ThreadStatic] private static int routineAnimationCalls;
    internal static double UpdateAnimationMs, LateAnimationMs;
    internal static int UpdateAnimationCalls, LateAnimationCalls;
    internal static string AnimationStatus = "not requested";
    private sealed class Marker
    {
        internal string Name = "";
        internal Recorder Recorder = null!;
        internal bool PreviousEnabled;
        internal long CpuNs, GpuNs, CpuBlocks, GpuBlocks;
        internal int CpuFrames, GpuFrames, Frames;
    }
    private static readonly List<Marker> markers = new();
    private static readonly Dictionary<string,string> unavailable = new();

    internal static void Begin()
    {
        End();
        Active = true;
        UpdateAnimationMs = LateAnimationMs = 0;
        UpdateAnimationCalls = LateAnimationCalls = 0;
        try
        {
            hooks = new Harmony(Plugin.Id + ".diagnostic");
            MethodInfo original = AccessTools.DeclaredMethod(typeof(Animator),"Update",new[]{typeof(float)})
                ?? throw new MissingMethodException("Animator.Update(float)");
            hooks.Patch(original,prefix:new HarmonyMethod(typeof(DiagnosticTiming),nameof(AnimationStart)),
                postfix:new HarmonyMethod(typeof(DiagnosticTiming),nameof(AnimationEnd)));
            AnimationStatus = "hook installed; native call interception must be confirmed by nonzero call counts";
        }
        catch (Exception ex)
        {
            hooks?.UnpatchSelf(); hooks = null;
            AnimationStatus = "unavailable: " + ex.Message;
            Plugin.Instance.Log.LogWarning("Animation timing " + AnimationStatus);
        }
        unavailable.Clear();
        foreach (string name in new[]{"PlayerLoop","Camera.Render","RenderLoop.Draw","Gfx.WaitForPresentOnGfxThread",
            "Gfx.WaitForCommands","Update.ScriptRunBehaviourUpdate","Animation.Update","Animators.Update","Animator.Update","Physics.Simulate","GC.Collect"})
        {
            try
            {
                var recorder = Recorder.Get(name);
                if (recorder == null || !recorder.isValid) { unavailable[name] = "marker unavailable in this player"; continue; }
                var marker = new Marker{Name=name,Recorder=recorder,PreviousEnabled=recorder.enabled};
                markers.Add(marker); // Keep ownership before enabling so cleanup can restore it.
                recorder.enabled = true;
            }
            catch (Exception ex) { unavailable[name] = ex.Message; }
        }
    }
    internal static void BeginCrowdRoutine()
    {
        if (!Active) return;
        CrowdDepth++;
        routineAnimationMs = 0; routineAnimationCalls = 0;
    }
    internal static void EndCrowdRoutine(bool late)
    {
        if (!Active) return;
        if (late) { LateAnimationMs = routineAnimationMs; LateAnimationCalls = routineAnimationCalls; }
        else { UpdateAnimationMs = routineAnimationMs; UpdateAnimationCalls = routineAnimationCalls; }
        CrowdDepth = Math.Max(0,CrowdDepth-1);
    }
    internal static void ClearCrowdScope() { CrowdDepth = 0; }
    private static void AnimationStart(out long __state)
    {
        __state = Active && CrowdDepth > 0 ? Stopwatch.GetTimestamp() : 0;
    }
    private static void AnimationEnd(long __state)
    {
        if (__state == 0) return;
        routineAnimationMs += (Stopwatch.GetTimestamp()-__state)*1000.0/Stopwatch.Frequency;
        routineAnimationCalls++;
    }
    internal static void SampleMarkers()
    {
        foreach (var m in markers)
        {
            try
            {
                m.Frames++;
                int blocks = m.Recorder.sampleBlockCount;
                m.CpuNs += m.Recorder.elapsedNanoseconds; m.CpuBlocks += blocks;
                if (blocks > 0) m.CpuFrames++;
                if (SystemInfo.supportsGpuRecorder)
                {
                    int gpuBlocks = m.Recorder.gpuSampleBlockCount;
                    m.GpuNs += m.Recorder.gpuElapsedNanoseconds; m.GpuBlocks += gpuBlocks;
                    if (gpuBlocks > 0) m.GpuFrames++;
                }
            }
            catch (Exception ex) { unavailable[m.Name] = "read failed: " + ex.Message; }
        }
    }
    internal static object Snapshot() => new
    {
        animationStatus = AnimationStatus,
        engineMarkers = markers.Select(m => new
        {
            name=m.Name, frames=m.Frames, cpuFramesWithSamples=m.CpuFrames, gpuFramesWithSamples=m.GpuFrames,
            averageCpuMs=m.CpuFrames > 0 && m.Frames > 0 ? (double?)(m.CpuNs/1e6/m.Frames) : null,
            averageGpuMs=m.GpuFrames > 0 && m.Frames > 0 ? (double?)(m.GpuNs/1e6/m.Frames) : null,
            cpuSampleBlocks=m.CpuBlocks, gpuSampleBlocks=m.GpuBlocks,
            status=unavailable.TryGetValue(m.Name,out var error) ? error : m.CpuFrames+m.GpuFrames == 0 ? "no recorded samples; cost unknown" : "samples recorded"
        }).ToArray(),
        unavailableMarkers=unavailable,
        supportsGpuRecorder=SystemInfo.supportsGpuRecorder,
        notes="Recorder timings may overlap and include multiple threads; do not sum them into frame time. GPU recorder data has a three-frame delay. Missing/no-sample markers are unavailable, not zero-cost. Diagnostic detours and recorder reads add overhead."
    };
    internal static void End()
    {
        Active = false; CrowdDepth = 0;
        hooks?.UnpatchSelf(); hooks = null;
        foreach (var m in markers)
        {
            try { m.Recorder.enabled = m.PreviousEnabled; }
            catch (Exception ex) { Plugin.Instance.Log.LogWarning("Restore profiler marker: " + ex.Message); }
        }
        markers.Clear();
    }
}
