// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using System.Drawing;
using System.Windows.Forms;
using System.Text.Json;

namespace OpenOLEDShifter;

internal static class Program
{
    public static readonly string DataDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenOLEDShifter");
    [STAThread]
    private static void Main(string[] args)
    {
        if (args.Contains("--self-test"))
        {
            ApplicationConfiguration.Initialize();
            SelfTests.Run();
            return;
        }

        using var mutex = new Mutex(true, @"Local\OpenOLEDShifter-" + Environment.UserName, out bool first);
        if (!first)
        {
            Native.ShowMessage("OpenOLEDShifter 已在托盘运行。");
            return;
        }

        Directory.CreateDirectory(DataDir);
        TrayApp? context = null;
        UnhandledExceptionEventHandler restore = (_, _) => context?.Restore();
        AppDomain.CurrentDomain.UnhandledException += restore;
        try
        {
            Environment.ExitCode = DesktopApp.Run(args, desktop =>
                context = new TrayApp(desktop, args.Contains("--smoke-test")));
        }
        catch (Exception error)
        {
            Log(error.ToString());
            Native.ShowMessage("运行失败：" + error.Message);
            Environment.ExitCode = 1;
        }
        finally
        {
            AppDomain.CurrentDomain.UnhandledException -= restore;
            context?.Dispose();
        }
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
    public int IntervalSeconds { get; set; } = ShiftPolicy.DefaultInterval;
    public int Radius { get; set; } = ShiftPolicy.DefaultRadius;
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
            p.IntervalSeconds = Math.Clamp(p.IntervalSeconds, ShiftPolicy.MinInterval, ShiftPolicy.MaxInterval);
            p.Radius = Math.Clamp(p.Radius, ShiftPolicy.MinRadius, ShiftPolicy.MaxRadius);
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
    public static List<Point> Points(int radius) =>
        ShiftPolicy.Points(radius).ConvertAll(offset => new Point(offset.X, offset.Y));

    public static int PingPong(int step, int count) => ShiftPolicy.PingPong(step, count);
}
