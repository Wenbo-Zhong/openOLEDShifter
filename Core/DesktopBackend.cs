// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
using System.Text.Json;

namespace OpenOLEDShifter;

[Flags]
// Optional setting controls in addition to enable/interval/radius/startup.
// A wallpaper-only backend uses the common enable switch rather than a second wallpaper toggle.
public enum DesktopFeatures
{
    None = 0, Wallpaper = 1, Icons = 2, AutoHideTaskbar = 4, PauseFullscreen = 8,
    FseWindows = 16, MonitorSelection = 32, WindowProcesses = 64,
    All = Wallpaper | Icons | AutoHideTaskbar | PauseFullscreen | FseWindows | MonitorSelection | WindowProcesses,
}
public sealed record MonitorChoice(string Id, string Name);
public sealed record DesktopCapabilities(string Name, DesktopFeatures Features, string Description,
    IReadOnlyList<MonitorChoice>? Monitors = null);
public sealed record DesktopState(DesktopSettings Settings, string Status);

public sealed record DesktopSettings
{
    public int IntervalSeconds { get; set; }
    public int Radius { get; set; }
    public bool Enabled { get; set; }
    public bool StartAtLogin { get; set; }
    public bool Wallpaper { get; set; }
    public bool Icons { get; set; }
    public bool AutoHideTaskbar { get; set; }
    public bool PauseFullscreen { get; set; }
    public bool FseWindows { get; set; }
    public string Monitor { get; set; }
    public string WindowProcesses { get; set; }

    [System.Text.Json.Serialization.JsonConstructor]
    public DesktopSettings(int IntervalSeconds = ShiftPolicy.DefaultInterval, int Radius = ShiftPolicy.DefaultRadius,
        bool Enabled = false, bool StartAtLogin = false, bool Wallpaper = true, bool Icons = false,
        bool AutoHideTaskbar = false, bool PauseFullscreen = true, bool FseWindows = false,
        string Monitor = "", string WindowProcesses = "")
    {
        this.IntervalSeconds = IntervalSeconds; this.Radius = Radius; this.Enabled = Enabled; this.StartAtLogin = StartAtLogin;
        this.Wallpaper = Wallpaper; this.Icons = Icons; this.AutoHideTaskbar = AutoHideTaskbar;
        this.PauseFullscreen = PauseFullscreen; this.FseWindows = FseWindows;
        this.Monitor = Monitor ?? ""; this.WindowProcesses = WindowProcesses ?? "";
    }
    public DesktopSettings Validate()
    {
        if (IntervalSeconds is < ShiftPolicy.MinInterval or > ShiftPolicy.MaxInterval)
            throw new ArgumentOutOfRangeException(nameof(IntervalSeconds), "位移间隔必须为 10–86400 秒。");
        if (Radius is < ShiftPolicy.MinRadius or > ShiftPolicy.MaxRadius)
            throw new ArgumentOutOfRangeException(nameof(Radius), "最大偏移必须为 1–8 个物理像素。");
        return this;
    }
    public DesktopSettings Normalize() => this with
    {
        IntervalSeconds = Math.Clamp(IntervalSeconds, ShiftPolicy.MinInterval, ShiftPolicy.MaxInterval),
        Radius = Math.Clamp(Radius, ShiftPolicy.MinRadius, ShiftPolicy.MaxRadius),
        Monitor = Monitor ?? "", WindowProcesses = WindowProcesses ?? "",
    };
}

// A platform adapter translates these commands to Win32/COM or GNOME's D-Bus API.
public interface IDesktopBackend
{
    DesktopCapabilities Capabilities { get; }
    Task<DesktopState> GetStateAsync(CancellationToken cancellationToken);
    Task<DesktopState> ApplyAsync(DesktopSettings settings, CancellationToken cancellationToken);
    Task<DesktopState> PauseAsync(CancellationToken cancellationToken);
    Task<DesktopState> ShiftAsync(CancellationToken cancellationToken);
}

public sealed class BackendConnection(IDesktopBackend backend)
{
    private readonly SemaphoreSlim gate = new(1, 1);
    public Task<DesktopState> RefreshAsync(CancellationToken token = default) => Call(backend.GetStateAsync, token);
    public Task<DesktopState> ApplyAsync(DesktopSettings settings, CancellationToken token = default)
    {
        var snapshot = (settings with { }).Validate();
        return Call(t => backend.ApplyAsync(snapshot, t), token);
    }
    public Task<DesktopState> PauseAsync(CancellationToken token = default) => Call(backend.PauseAsync, token);
    public Task<DesktopState> ShiftAsync(CancellationToken token = default) => Call(backend.ShiftAsync, token);
    private async Task<DesktopState> Call(Func<CancellationToken, Task<DesktopState>> operation, CancellationToken token)
    {
        await gate.WaitAsync(token);
        try { return await operation(token); }
        finally { gate.Release(); }
    }
}

public static class AtomicJsonStore
{
    private static readonly object Gate = new();
    public static void Write<T>(string path, T value)
    {
        lock (Gate) WriteSnapshot(path, value);
    }
    private static void WriteSnapshot<T>(string path, T value)
    {
        path = Path.GetFullPath(path);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var file = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                JsonSerializer.Serialize(file, value, new JsonSerializerOptions { WriteIndented = true });
                file.Flush(true);
            }
            File.Move(temp, path, true);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
}

public sealed class ShiftCycle
{
    public int Phase { get; private set; }

    public PixelOffset Next(int radius) => Advance(radius).Offset;

    public ShiftFrame Advance(int radius)
    {
        var frames = ShiftPolicy.Frames(radius);
        Phase = frames[Phase % frames.Count].NextPhase;
        return frames[Phase];
    }

    public void Reset() => Phase = 0;
}
