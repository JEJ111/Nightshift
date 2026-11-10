using BepInEx;
using BepInEx.Configuration;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using UnityEngine;
using Nivalis;
using System.Diagnostics;
using System.Security.Cryptography;

namespace NightShift;

[BepInPlugin(Id, "NightShift", Version)]
public sealed class Plugin : BasePlugin
{
    public const string Id = "local.nivalis.nightshift";
    public const string Version = "0.5.8";
    internal static Plugin Instance = null!;
    internal ConfigEntry<bool> Optimize = null!;
    internal ConfigEntry<bool> Hud = null!;
    internal ConfigEntry<int> CaptureSeconds = null!;
    internal ConfigEntry<DlssMode> RenderMode = null!;
    internal ConfigEntry<FrameGenMode> FrameGeneration = null!;
    internal ConfigEntry<bool> DevelopmentControl = null!;
    private Harmony? harmony;
    private Driver? driver;

    public override void Load()
    {
        Instance = this;
        Optimize = Config.Bind("Optimization", "CollapseUnmanagedLods", false,
            "Experimental: keep the most detailed enabled mesh in unmanaged sibling LOD sets. Available only through configuration. Managed LODGroups, shadow meshes and NPC simulation are preserved.");
        Hud = Config.Bind("Diagnostics", "ShowHud", true, "F7 toggles the overlay.");
        CaptureSeconds = Config.Bind("Diagnostics", "CaptureSeconds", 30, "Internal diagnostic duration, clamped to 5-120 seconds.");
        RenderMode = Config.Bind("Rendering", "DLSSMode", DlssMode.DLAA, "F5 toggles native-resolution DLAA. Starts with DLAA enabled. DLSS upscaling is coming soon.");
        // Start with native-resolution DLAA. Persisted legacy upscaling modes
        // are intentionally not exposed in this release.
        RenderMode.Value = DlssMode.DLAA;
        FrameGeneration=Config.Bind("Rendering","FrameGeneration",FrameGenMode.Off,"F11 cycles OFF / 2x / 3x / 4x, skipping unsupported GPU modes. Frame generation starts OFF each launch.");
        FrameGeneration.Value=FrameGenMode.Off;
        DevelopmentControl=Config.Bind("Development","EnableControlFile",false,"Local render validation only: poll a bounded render-test-request.json. Disabled for normal play; captures can stall the GPU.");
        Log.LogInfo($"NightShift {Version} | Unity {Application.unityVersion} | {SystemInfo.graphicsDeviceName} | {SystemInfo.graphicsDeviceType}");
        if (Application.unityVersion != "2020.3.44f1" && Application.unityVersion != "2020.3.49f1")
        {
            Log.LogError("Unvalidated Unity version. Refusing to patch this build.");
            return;
        }
        string gameHash;
        using (var file = System.IO.File.OpenRead(System.IO.Path.Combine(Paths.GameRootPath,"GameAssembly.dll")))
        {
            using var sha = SHA256.Create();
            string hash = Convert.ToHexString(sha.ComputeHash(file));
            gameHash = hash;
            if (hash != "9A0E32C2D09A5025F867D29BF39B9BEDD0715B513456617FBFD82C581E1A376D" && hash != "D7D7FEF8B76699AE6A9F70B111C239B013BA00261A02A85BDB394BDE38C46504" && hash != "0DA6AAC5209F504DA743ABD7926F6F528010E2CA7B884B4A20F5198F42F1A26D" && hash != "6BFAE1C6445A755B915550B005994BF029208C8B276CF3DA59A4CCE2724435D3" && hash != "F67D5C384A12B67EF4458633AE48D3B3986C91E466C5CB8DADB1130EE2DA244A" && hash != "BB14ABA0BE68D2A2D2968C49B1800FE753DE8CCB7208C1FE8A726AD326FB3C68" && hash != "C65362434D61434A6FF5BFF771E7C2C44F2AEEABFDB1398C10B25FD9CBCF424E" && hash != "8A1265FF519B6D876E5403684CA9581FB18825FA6F818DCA1C5346AD8C147E46" && hash != "86545EF32D8D28969E166FA4DCF8DAD7922315ED8F1913A22D8635848633639A" && hash != "5C6760DD4BAFDBD423D7B6BFE6535908F2FB8172B50FA7C88F591092721BC9DE")
            {
                Log.LogError("Unvalidated game binary. Refusing hooks until this update is checked.");
                return;
            }
        }
        bool updatedEngine = gameHash == "BB14ABA0BE68D2A2D2968C49B1800FE753DE8CCB7208C1FE8A726AD326FB3C68" || gameHash == "C65362434D61434A6FF5BFF771E7C2C44F2AEEABFDB1398C10B25FD9CBCF424E" || gameHash == "8A1265FF519B6D876E5403684CA9581FB18825FA6F818DCA1C5346AD8C147E46" || gameHash == "86545EF32D8D28969E166FA4DCF8DAD7922315ED8F1913A22D8635848633639A" || gameHash == "5C6760DD4BAFDBD423D7B6BFE6535908F2FB8172B50FA7C88F591092721BC9DE";
        string expectedUnityHash = updatedEngine ? "CFB43640C382ACA31FD2D482A4DD83122E23510F9893F92DFD0ECABFE30306A6" : "8B457114AF608690521AFB6D3770A27AC54197411EFBD184DED833DD38DB2686";
        string expectedUnityVersion = updatedEngine ? "2020.3.49f1" : "2020.3.44f1";
        using (var player = System.IO.File.OpenRead(System.IO.Path.Combine(Paths.GameRootPath, "UnityPlayer.dll")))
        {
            using var playerSha = SHA256.Create();
            if (Application.unityVersion != expectedUnityVersion || Convert.ToHexString(playerSha.ComputeHash(player)) != expectedUnityHash)
            {
                Log.LogError("Unvalidated game/Unity renderer pair. Refusing to patch this build.");
                return;
            }
        }
        if (System.IO.File.Exists(System.IO.Path.Combine(Paths.PluginPath,"Lumen","Lumen.dll")) ||
            AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "Lumen"))
        {
            Log.LogError("Lumen detected. Refusing overlapping renderer patches. Use one optimizer at a time.");
            return;
        }
        try
        {
            harmony = new Harmony(Id);
            Registry.Install(harmony);
            CrowdTiming.Install(harmony);
            DlssRenderer.Install(harmony);
            try { FrameGenRenderer.Install(); }
            catch(Exception error) { FrameGenRenderer.Uninstall(); Log.LogError("Frame-generation integration unavailable; DLSS retained: "+error); }
            driver = AddComponent<Driver>();
            Log.LogInfo("Ready: F11 frame generation OFF/2x/3x/4x; F5 DLAA on/off; F7 show/hide menu. DLSS upscaling coming soon.");
        }
        catch (Exception ex)
        {
            FrameGenRenderer.Uninstall();
            DlssRenderer.Uninstall();
            harmony?.UnpatchSelf();
            Log.LogError($"Startup failed; hooks removed: {ex}");
        }
    }

    public override bool Unload()
    {
        driver?.Stop();
        FrameGenRenderer.Uninstall();
        DlssRenderer.Uninstall();
        harmony?.UnpatchSelf();
        Registry.Clear();
        return true;
    }
}

internal static class CrowdTiming
{
    internal static double UpdateMs, LateMs;
    internal static void Install(Harmony h)
    {
        foreach (string name in new[]{"UpdateAll","LateUpdateAll"})
        {
            var original = AccessTools.DeclaredMethod(typeof(Character),name) ?? throw new MissingMethodException(name);
            h.Patch(original, prefix: new HarmonyMethod(typeof(CrowdTiming),nameof(Start)),
                postfix: new HarmonyMethod(typeof(CrowdTiming),name == "UpdateAll" ? nameof(EndUpdate) : nameof(EndLate)),
                finalizer: new HarmonyMethod(typeof(CrowdTiming),nameof(FinalizeScope)));
        }
        Plugin.Instance.Log.LogInfo("Crowd UpdateAll/LateUpdateAll timing hooks installed (no NPC updates skipped).");
    }
    private static void Start(out long __state) { __state = Stopwatch.GetTimestamp(); DiagnosticTiming.BeginCrowdRoutine(); }
    private static void EndUpdate(long __state) { UpdateMs = (Stopwatch.GetTimestamp()-__state)*1000.0/Stopwatch.Frequency; DiagnosticTiming.EndCrowdRoutine(false); }
    private static void EndLate(long __state) { LateMs = (Stopwatch.GetTimestamp()-__state)*1000.0/Stopwatch.Frequency; DiagnosticTiming.EndCrowdRoutine(true); }
    private static void FinalizeScope() { DiagnosticTiming.ClearCrowdScope(); }
}

internal static class Registry
{
    internal static readonly Dictionary<int, Character> Live = new();
    internal static event Action<int>? Releasing;
    internal static void Install(Harmony h)
    {
        var enable = AccessTools.DeclaredMethod(typeof(Character), "OnEnable") ?? throw new MissingMethodException("Character.OnEnable");
        var disable = AccessTools.DeclaredMethod(typeof(Character), "OnDisable") ?? throw new MissingMethodException("Character.OnDisable");
        var visible = AccessTools.DeclaredMethod(typeof(Character), "SetVisible") ?? throw new MissingMethodException("Character.SetVisible");
        // Only lifecycle/visibility events. No hooks on character AI, movement or simulation updates.
        h.Patch(enable, postfix: new HarmonyMethod(typeof(Registry), nameof(Enabled)));
        h.Patch(disable, prefix: new HarmonyMethod(typeof(Registry), nameof(Disabled)));
        h.Patch(visible, prefix: new HarmonyMethod(typeof(Registry), nameof(BeforeVisibilityChanged)),
            postfix: new HarmonyMethod(typeof(Registry), nameof(VisibilityChanged)));
        Plugin.Instance.Log.LogInfo("Three character lifecycle/visibility hooks installed; no scene-wide scans.");
    }
    private static void Enabled(Character __instance)
    {
        try
        {
            if (__instance == null) return;
            int id = __instance.GetInstanceID();
            Releasing?.Invoke(id);
            Live[id] = __instance;
            Driver.Active?.Queue(id);
        }
        catch (Exception ex) { Plugin.Instance.Log.LogWarning($"Character registration: {ex.Message}"); }
    }
    private static void Disabled(Character __instance)
    {
        try
        {
            if (__instance == null) return;
            int id = __instance.GetInstanceID();
            Releasing?.Invoke(id);
            Live.Remove(id);
        }
        catch (Exception ex) { Plugin.Instance.Log.LogWarning($"Character release: {ex.Message}"); }
    }
    private static void BeforeVisibilityChanged(Character __instance)
    {
        try
        {
            if (__instance != null) Driver.Active?.Restore(__instance.GetInstanceID());
        }
        catch (Exception ex) { Plugin.Instance.Log.LogWarning($"Visibility restore: {ex.Message}"); }
    }
    private static void VisibilityChanged(Character __instance)
    {
        try
        {
            if (__instance == null) return;
            // The original game's visibility state takes ownership first, then only
            // currently enabled unmanaged sibling renderers may be optimized again.
            int id = __instance.GetInstanceID();
            Driver.Active?.Queue(id);
        }
        catch (Exception ex) { Plugin.Instance.Log.LogWarning($"Visibility notification: {ex.Message}"); }
    }
    internal static void Clear() { Live.Clear(); }
}
