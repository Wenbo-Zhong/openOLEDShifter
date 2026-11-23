// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using Microsoft.Win32;
using System.Diagnostics;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform;
using Avalonia.Threading;
using Screen = System.Windows.Forms.Screen;


namespace OpenOLEDShifter;

internal sealed class TrayApp : IDisposable
{
    private readonly Preferences prefs = Preferences.Load();
    private readonly ShiftEngine engine;
    private readonly IClassicDesktopStyleApplicationLifetime desktop;
    private readonly TrayIcon tray;
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(2) };
    private SettingsWindow? window;
    private DateTime due = DateTime.UtcNow;
    private bool suspended, locked, stopped, exiting;
    private readonly bool smoke;
    private string lastReason = "";
    private string status = "正在启动";

    public TrayApp(IClassicDesktopStyleApplicationLifetime desktop, bool smokeTest)
    {
        this.desktop = desktop;
        smoke = smokeTest;
        engine = smoke ? new ShiftEngine(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))) : new ShiftEngine();
        var menu = new NativeMenu();
        AddMenuItem("打开设置", ShowSettings);
        AddMenuItem("暂停 / 继续", Toggle);
        AddMenuItem("立即位移一次", () => Tick(true));
        AddMenuItem("恢复原位", () => { prefs.Enabled = false; prefs.Save(); Tick(); });
        menu.Items.Add(new NativeMenuItemSeparator());
        AddMenuItem("打开日志目录", () => Process.Start(new ProcessStartInfo("explorer.exe", Program.DataDir) { UseShellExecute = true }));
        AddMenuItem("退出并恢复", Exit);

        using var icon = AssetLoader.Open(DesktopApp.IconUri);
        tray = new TrayIcon
        {
            Icon = new WindowIcon(icon), ToolTipText = "OpenOLEDShifter", IsVisible = true, Menu = menu,
        };
        tray.Clicked += (_, _) => ShowSettings();
        timer.Tick += (_, _) => Tick();
        desktop.Exit += (_, _) => Dispose();
        if (smoke)
        {
            ShowSettings();
            DesktopApp.SmokeTest(window!, desktop);
            return;
        }

        void AddMenuItem(string label, Action action)
        {
            var item = new NativeMenuItem(label) { IsEnabled = !smoke || label == "打开设置" };
            item.Click += (_, _) =>
            {
                try { action(); }
                catch (Exception error) { Program.Log(error.ToString()); EmergencyStop(error.Message); }
            };
            menu.Items.Add(item);
        }

        // A previous interrupted run is recovered before any new baseline is taken.
        if (!engine.Restore()) { prefs.Enabled = false; prefs.Save(); ShowSettings(); }
        SystemEvents.PowerModeChanged += PowerChanged;
        SystemEvents.SessionSwitch += SessionChanged;
        SystemEvents.DisplaySettingsChanged += DisplaysChanged;
        SystemEvents.SessionEnding += SessionEnding;
        due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds);
        timer.Start(); Tick();
        if (Native.VendorRunning()) ShowSettings();
    }
    private string Reason()
    {
        if (stopped) return "出错后已暂停，请检查日志";
        if (!prefs.Enabled) return "已暂停";
        if (suspended) return "系统休眠，已暂停";
        if (locked) return "会话已锁定，已暂停";
        if (Native.VendorRunning()) return "检测到华硕 OLED Shifter，自动暂停以避免冲突";
        if (prefs.PauseFullscreen && Native.Fullscreen(prefs.Monitor, prefs.FseWindows)) return "全屏应用运行中，自动暂停";
        if (prefs.Monitor != "*" && !Screen.AllScreens.Any(s => s.DeviceName == prefs.Monitor)) return "所选屏幕未连接，已暂停";
        return "";
    }
    private void Tick(bool force = false)
    {
        if (exiting || smoke) return;
        try
        {
            engine.RestoreInactiveFse();
            string reason = Reason();
            if (reason.Length > 0)
            {
                if (lastReason != reason)
                {
                    if (!engine.Restore()) { stopped = true; reason = engine.Detail; }
                    due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds);
                }
                lastReason = reason; UpdateStatus(reason); return;
            }
            if (lastReason.Length > 0) due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds);
            lastReason = "";
            if (force || DateTime.UtcNow >= due)
            { engine.Shift(prefs); due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds); }
            UpdateStatus($"运行中 · {engine.Detail}\n下次位移：{due.ToLocalTime():HH:mm:ss}");
        }
        catch (Exception e) { Program.Log(e.ToString()); EmergencyStop(e.Message); }
    }
    private void UpdateStatus(string text)
    {
        if (status != text) Program.Log(text.Replace('\n', ' '));
        status = text; tray.ToolTipText = text.Length > 60 ? text[..60] : text;
        window?.SetStatus(status);
    }
    public void Restore() { if (!smoke) engine.Restore(); }
    public void EmergencyStop(string error)
    { stopped = true; Restore(); UpdateStatus("异常暂停：" + error); }
    private void Toggle() { stopped = false; prefs.Enabled = !prefs.Enabled; prefs.Save(); Tick(); }
    private void Apply()
    {
        if (!engine.Restore())
        {
            stopped = true;
            UpdateStatus(engine.Detail);
            throw new InvalidOperationException(engine.Detail);
        }
        stopped = false; prefs.Save(); lastReason = "";
        due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds); Tick();
    }

    private void ShowSettings()
    {
        if (window is null)
        {
            var monitors = Screen.AllScreens.Select(screen => new MonitorChoice(screen.DeviceName,
                $"{screen.DeviceName} · {screen.Bounds.Width}×{screen.Bounds.Height}" + (screen.Primary ? " · 主屏" : "")))
                .Append(new MonitorChoice("*", "所有屏幕")).ToArray();
            window = new SettingsWindow(monitors, SaveSettings, () =>
            {
                prefs.Enabled = false;
                Apply();
                return Task.CompletedTask;
            }, () =>
            {
                Tick(true);
                return Task.CompletedTask;
            });
            window.Closed += (_, _) => window = null;
            window.LoadSettings(new DesktopSettings(prefs.IntervalSeconds, prefs.Radius, prefs.Enabled, Startup.Enabled,
                prefs.Wallpaper, prefs.Icons, prefs.AutoHideTaskbar, prefs.PauseFullscreen, prefs.FseWindows,
                prefs.Monitor, prefs.WindowProcesses));
            window.SetAvailable(!smoke);
            window.SetStatus(status);
        }
        window.Show();
        window.WindowState = Avalonia.Controls.WindowState.Normal;
        window.Activate();
    }

    private Task SaveSettings(DesktopSettings settings)
    {
        Startup.Enabled = settings.StartAtLogin;
        prefs.Enabled = settings.Enabled;
        prefs.Wallpaper = settings.Wallpaper;
        prefs.Icons = settings.Icons;
        prefs.AutoHideTaskbar = settings.AutoHideTaskbar;
        prefs.PauseFullscreen = settings.PauseFullscreen;
        prefs.FseWindows = settings.FseWindows;
        prefs.IntervalSeconds = settings.IntervalSeconds;
        prefs.Radius = settings.Radius;
        prefs.Monitor = settings.Monitor;
        prefs.WindowProcesses = settings.WindowProcesses;
        Apply();
        return Task.CompletedTask;
    }

    private void PowerChanged(object sender, PowerModeChangedEventArgs e)
    { Post(() => { suspended = e.Mode == PowerModes.Suspend || (suspended && e.Mode != PowerModes.Resume); Tick(); }); }
    private void SessionChanged(object sender, SessionSwitchEventArgs e)
    { Post(() => { if (e.Reason == SessionSwitchReason.SessionLock) locked = true; if (e.Reason == SessionSwitchReason.SessionUnlock) locked = false; Tick(); }); }
    private void DisplaysChanged(object? sender, EventArgs e)
    { Post(() => { if (!engine.Restore()) stopped = true; due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds); Tick(); }); }
    private void SessionEnding(object sender, SessionEndingEventArgs e) => Post(Exit);

    private void Post(Action action)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (exiting) return;
            try { action(); }
            catch (Exception error) { Program.Log(error.ToString()); EmergencyStop(error.Message); }
        });
    }

    private void Exit()
    {
        try { Dispose(); }
        finally { desktop.Shutdown(); }
    }

    public void Dispose()
    {
        if (exiting) return;
        exiting = true;
        timer.Stop();
        try { Restore(); }
        finally
        {
            if (!smoke)
            {
                SystemEvents.PowerModeChanged -= PowerChanged;
                SystemEvents.SessionSwitch -= SessionChanged;
                SystemEvents.DisplaySettingsChanged -= DisplaysChanged;
                SystemEvents.SessionEnding -= SessionEnding;
            }
            tray.Dispose();
        }
    }
}

internal static class Startup
{
    private const string Key = @"Software\Microsoft\Windows\CurrentVersion\Run";
    public static bool Enabled
    {
        get { using var k = Registry.CurrentUser.OpenSubKey(Key); return k?.GetValue("OpenOLEDShifter") is string; }
        set
        {
            using var k = Registry.CurrentUser.CreateSubKey(Key);
            if (value) k.SetValue("OpenOLEDShifter", "\"" + Environment.ProcessPath + "\"");
            else k.DeleteValue("OpenOLEDShifter", false);
        }
    }
}
