// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using Microsoft.Win32;
using System.Diagnostics;
using System.Text.Json;

namespace OpenOLEDShifter;

internal static class Program
{
    public static readonly string DataDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenOLEDShifter");
    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if (args.Contains("--self-test")) { SelfTests.Run(); return; }
        using var mutex = new Mutex(true, @"Local\OpenOLEDShifter-" + Environment.UserName, out bool first);
        if (!first) { MessageBox.Show("OpenOLEDShifter 已在托盘运行。", "OpenOLEDShifter"); return; }
        Directory.CreateDirectory(DataDir);
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        TrayApp context;
        try { context = new TrayApp(args.Contains("--smoke-test")); }
        catch (Exception e) { Log(e.ToString()); MessageBox.Show("启动失败：" + e.Message + "\n恢复记录不会被覆盖。", "OpenOLEDShifter"); return; }
        using var lifetime = context;
        Application.ThreadException += (_, e) => { Log(e.Exception.ToString()); context.EmergencyStop(e.Exception.Message); };
        AppDomain.CurrentDomain.UnhandledException += (_, _) => context.Restore();
        Application.Run(context);
    }
    public static void Log(string message)
    {
        try
        {
            Directory.CreateDirectory(DataDir);
            string file = Path.Combine(DataDir, "app.log");
            if (File.Exists(file) && new FileInfo(file).Length > 1_000_000) File.Move(file, file + ".previous", true);
            File.AppendAllText(file, $"{DateTimeOffset.Now:O} {message}\n");
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}

internal sealed class Preferences
{
    public int IntervalSeconds { get; set; } = 200;
    public int Radius { get; set; } = 1;
    public bool Enabled { get; set; } = true;
    public bool Wallpaper { get; set; } = true;
    public bool Icons { get; set; } = Screen.AllScreens.Length == 1;
    public bool AutoHideTaskbar { get; set; }
    public bool PauseFullscreen { get; set; } = true;
    public bool FseWindows { get; set; } = true;
    public string Monitor { get; set; } = Screen.PrimaryScreen?.DeviceName ?? "";
    public string WindowProcesses { get; set; } = "";
    public static Preferences Load()
    {
        string path = Path.Combine(Program.DataDir, "settings.json");
        if (!File.Exists(path)) return new();
        try
        {
            var p = JsonSerializer.Deserialize<Preferences>(File.ReadAllText(path)) ?? new();
            p.IntervalSeconds = Math.Clamp(p.IntervalSeconds, 10, 86400);
            p.Radius = Math.Clamp(p.Radius, 1, 8);
            p.Monitor ??= ""; p.WindowProcesses ??= "";
            return p;
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        { Program.Log("Settings load failed: " + e.Message); return new(); }
    }
    public void Save() => Store.Write(Path.Combine(Program.DataDir, "settings.json"), this);
}

internal static class Store
{
    public static void Write<T>(string path, T value)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temp = path + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(value, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temp, path, true);
    }
}

internal static class Orbit
{
    // A square spiral: origin, right, down, left, up; then expand one ring.
    public static List<Point> Points(int radius)
    {
        if (radius is < 1 or > 8) throw new ArgumentOutOfRangeException(nameof(radius));
        var result = new List<Point> { Point.Empty };
        int x = 0, y = 0, direction = 0, length = 1;
        Point[] dirs = [new(1, 0), new(0, 1), new(-1, 0), new(0, -1)];
        int count = (radius * 2 + 1) * (radius * 2 + 1);
        while (result.Count < count)
        {
            for (int repeat = 0; repeat < 2; repeat++)
            {
                Point d = dirs[direction++ % 4];
                for (int step = 0; step < length && result.Count < count; step++)
                { x += d.X; y += d.Y; result.Add(new Point(x, y)); }
            }
            length++;
        }
        return result;
    }
    public static int PingPong(int step, int count)
    {
        if (count <= 1) return 0;
        int period = 2 * (count - 1), n = ((step % period) + period) % period;
        return n < count ? n : period - n;
    }
}
