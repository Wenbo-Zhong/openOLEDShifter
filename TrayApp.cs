// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using Microsoft.Win32;
using System.Diagnostics;

namespace OpenOLEDShifter;

internal sealed class TrayApp : ApplicationContext
{
    private readonly Preferences prefs = Preferences.Load();
    private readonly ShiftEngine engine = new();
    private readonly NotifyIcon tray;
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 2000 };
    private SettingsForm? window;
    private DateTime due = DateTime.UtcNow;
    private bool suspended, locked, stopped, exiting, smoke;
    private string lastReason = "";
    private string status = "正在启动";
    public TrayApp(bool smokeTest)
    {
        smoke = smokeTest;
        InitializeDispatcher();
        var menu = new ContextMenuStrip();
        menu.Items.Add("打开设置", null, (_, _) => ShowSettings());
        menu.Items.Add("暂停 / 继续", null, (_, _) => Toggle());
        menu.Items.Add("立即位移一次", null, (_, _) => Tick(true));
        menu.Items.Add("恢复原位", null, (_, _) => { prefs.Enabled = false; prefs.Save(); Restore(); UpdateStatus("已暂停并恢复原位"); });
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("打开日志", null, (_, _) => Process.Start(new ProcessStartInfo("explorer.exe", Program.DataDir) { UseShellExecute = true }));
        menu.Items.Add("退出并恢复", null, (_, _) => ExitThread());
        tray = new NotifyIcon { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath) ?? SystemIcons.Application, Text = "OpenOLEDShifter", Visible = true, ContextMenuStrip = menu };
        tray.DoubleClick += (_, _) => ShowSettings();
        timer.Tick += (_, _) => Tick();
        if (smoke)
        {
            ShowSettings();
            var smokeTimer = new System.Windows.Forms.Timer { Interval = 800 };
            smokeTimer.Tick += (_, _) =>
            {
                smokeTimer.Stop();
                Directory.CreateDirectory("artifacts");
                using var image = new Bitmap(window!.Width, window.Height);
                window.DrawToBitmap(image, new Rectangle(Point.Empty, image.Size));
                image.Save("artifacts/settings-preview.png");
                File.WriteAllText("artifacts/smoke-test.txt", "Tray icon created; settings rendered; no desktop mutation.\n");
                smokeTimer.Dispose(); ExitThread();
            };
            smokeTimer.Start(); return;
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
        status = text; tray.Text = text.Length > 60 ? text[..60] : text;
        window?.SetStatus(status);
    }
    public void Restore() { if (!smoke) engine.Restore(); }
    public void EmergencyStop(string error)
    { stopped = true; Restore(); UpdateStatus("异常暂停：" + error); }
    private void Toggle() { stopped = false; prefs.Enabled = !prefs.Enabled; prefs.Save(); Tick(); }
    private void Apply()
    {
        if (!engine.Restore()) { stopped = true; UpdateStatus(engine.Detail); return; }
        stopped = false; prefs.Save(); lastReason = "";
        due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds); Tick();
    }
    private void ShowSettings()
    {
        if (window == null || window.IsDisposed)
            window = new SettingsForm(prefs, status, Apply, () => { prefs.Enabled = false; Apply(); }, () => Tick(true), smoke);
        window.Show(); window.WindowState = FormWindowState.Normal; window.Activate();
    }
    private void PowerChanged(object sender, PowerModeChangedEventArgs e)
    { Post(() => { suspended = e.Mode == PowerModes.Suspend || (suspended && e.Mode != PowerModes.Resume); Tick(); }); }
    private void SessionChanged(object sender, SessionSwitchEventArgs e)
    { Post(() => { if (e.Reason == SessionSwitchReason.SessionLock) locked = true; if (e.Reason == SessionSwitchReason.SessionUnlock) locked = false; Tick(); }); }
    private void DisplaysChanged(object? sender, EventArgs e)
    { Post(() => { if (!engine.Restore()) stopped = true; due = DateTime.UtcNow.AddSeconds(prefs.IntervalSeconds); Tick(); }); }
    private void SessionEnding(object sender, SessionEndingEventArgs e) { Post(() => ExitThread()); }
    private readonly Control dispatcher = new();
    private void Post(Action action)
    {
        if (exiting) return;
        try { if (dispatcher.IsHandleCreated) dispatcher.BeginInvoke(action); }
        catch (InvalidOperationException) { }
    }
    protected override void OnMainFormClosed(object? sender, EventArgs e) { }
    protected override void ExitThreadCore()
    {
        if (exiting) return; exiting = true; timer.Stop();
        Restore();
        if (!smoke)
        {
            SystemEvents.PowerModeChanged -= PowerChanged; SystemEvents.SessionSwitch -= SessionChanged;
            SystemEvents.DisplaySettingsChanged -= DisplaysChanged; SystemEvents.SessionEnding -= SessionEnding;
        }
        tray.Visible = false; tray.Dispose(); timer.Dispose();
        window?.Dispose(); dispatcher.Dispose(); base.ExitThreadCore();
    }
    protected override void Dispose(bool disposing)
    { if (disposing && !exiting) ExitThreadCore(); base.Dispose(disposing); }
    public void InitializeDispatcher() => _ = dispatcher.Handle;
}

internal sealed class SettingsForm : Form
{
    private readonly Label statusLabel;
    public SettingsForm(Preferences p, string status, Action apply, Action restore, Action shift, bool smoke)
    {
        SuspendLayout();
        AutoScaleDimensions = new SizeF(96, 96);
        AutoScaleMode = AutoScaleMode.Dpi;
        Text = "OpenOLEDShifter · OLED 像素位移";
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath) ?? SystemIcons.Application;
        Font = new Font("Microsoft YaHei UI", 10);
        ClientSize = new Size(610, 750);
        MinimumSize = new Size(580, 650); StartPosition = FormStartPosition.CenterScreen;
        bool dark = (int?)Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme", 1) == 0;
        BackColor = dark ? Color.FromArgb(28, 31, 36) : Color.FromArgb(247, 248, 250);
        ForeColor = dark ? Color.FromArgb(240, 243, 247) : Color.FromArgb(30, 35, 43);
        var flow = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new Padding(22), AutoScroll = true };
        Controls.Add(flow);
        flow.Controls.Add(new Label { Text = "让静态桌面轻轻移动", Font = new Font(Font.FontFamily, 17, FontStyle.Bold), AutoSize = true, Margin = new Padding(0, 0, 0, 14) });
        statusLabel = new Label { Text = status, Width = 510, Height = 64, BackColor = dark ? Color.FromArgb(40, 51, 64) : Color.FromArgb(225, 237, 249), Padding = new Padding(10), Margin = new Padding(0, 0, 0, 16) };
        flow.Controls.Add(statusLabel);
        var enabled = Check("启用定时位移", p.Enabled);
        var wallpaper = Check("位移壁纸（仅静态图片）", p.Wallpaper);
        var icons = Check("位移桌面图标层（影响所有屏幕）", p.Icons);
        var taskbar = Check("运行时自动隐藏任务栏，退出恢复", p.AutoHideTaskbar);
        var fullscreen = Check("全屏应用运行时暂停并恢复原位", p.PauseFullscreen);
        var fse = Check("支持原版 FSE 白名单窗口（Xbox / 掌机界面）", p.FseWindows);
        foreach (var c in new[] { enabled, wallpaper, icons, taskbar, fullscreen, fse }) flow.Controls.Add(c);
        var interval = new NumericUpDown { Minimum = 10, Maximum = 86400, Value = p.IntervalSeconds, Width = 110 };
        var radius = new NumericUpDown { Minimum = 1, Maximum = 8, Value = p.Radius, Width = 110 };
        flow.Controls.Add(Row("位移间隔（秒）", interval)); flow.Controls.Add(Row("最大偏移（物理像素）", radius));
        var monitors = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 285 };
        var items = Screen.AllScreens.Select(s => new MonitorChoice(s.DeviceName, $"{s.DeviceName} · {s.Bounds.Width}×{s.Bounds.Height}" + (s.Primary ? " · 主屏" : ""))).ToList();
        items.Add(new MonitorChoice("*", "所有屏幕")); monitors.DataSource = items; monitors.DisplayMember = "Name";
        monitors.SelectedItem = items.FirstOrDefault(x => x.Id == p.Monitor) ?? items[0];
        flow.Controls.Add(Row("壁纸 / 窗口目标屏幕", monitors));
        flow.Controls.Add(new Label { Text = "额外位移的应用进程（逗号分隔，可留空）", AutoSize = true, Margin = new Padding(0, 10, 0, 4) });
        var apps = new TextBox { Width = 510, Text = p.WindowProcesses, PlaceholderText = "例如 notepad.exe；最大化窗口和全屏窗口不移动" };
        flow.Controls.Add(apps);
        var startup = Check("登录 Windows 后启动", Startup.Enabled); flow.Controls.Add(startup);
        var buttons = new FlowLayoutPanel { Width = 515, Height = 45, Margin = new Padding(0, 12, 0, 3) };
        var save = Button("保存设置", () =>
        {
            if (smoke) return;
            p.Enabled = enabled.Checked; p.Wallpaper = wallpaper.Checked; p.Icons = icons.Checked;
            p.AutoHideTaskbar = taskbar.Checked; p.PauseFullscreen = fullscreen.Checked;
            p.FseWindows = fse.Checked;
            p.IntervalSeconds = (int)interval.Value; p.Radius = (int)radius.Value;
            p.Monitor = ((MonitorChoice)monitors.SelectedItem!).Id; p.WindowProcesses = apps.Text;
            try { Startup.Enabled = startup.Checked; apply(); }
            catch (Exception e) { MessageBox.Show(this, e.Message, "设置未完整保存"); }
        });
        buttons.Controls.Add(save); buttons.Controls.Add(Button("立即位移", shift));
        buttons.Controls.Add(Button("暂停并还原", () => { enabled.Checked = false; restore(); }));
        flow.Controls.Add(buttons);
        flow.Controls.Add(new Label { Text = "关闭此窗口后继续在托盘运行。检测到华硕原版时自动暂停。\n此工具降低静态图像停留，不保证消除 OLED 烧屏。", Width = 510, Height = 50, Font = new Font(Font.FontFamily, 9), ForeColor = dark ? Color.Silver : Color.DimGray });
        foreach (Control c in flow.Controls)
            if (c is Panel row) foreach (Control child in row.Controls) Style(child, dark);
            else Style(c, dark);
        FormClosing += (_, e) => { if (e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; Hide(); } };
        ResumeLayout(true);
    }
    private static CheckBox Check(string text, bool value) => new() { Text = text, Checked = value, AutoSize = true, Margin = new Padding(0, 4, 0, 5) };
    private static FlowLayoutPanel Row(string label, Control control)
    {
        var row = new FlowLayoutPanel { Width = 515, Height = 38, Margin = new Padding(0, 4, 0, 0) };
        row.Controls.Add(new Label { Text = label, Width = 215, AutoSize = false, Height = 30, TextAlign = ContentAlignment.MiddleLeft }); row.Controls.Add(control); return row;
    }
    private static Button Button(string text, Action click)
    {
        var b = new Button { Text = text, Width = 145, Height = 36, FlatStyle = FlatStyle.Flat };
        b.FlatAppearance.BorderSize = 2; b.Click += (_, _) => click(); return b;
    }
    private static void Style(Control c, bool dark)
    {
        if (c is TextBox or ComboBox or NumericUpDown or System.Windows.Forms.Button)
        { c.BackColor = dark ? Color.FromArgb(48, 54, 64) : Color.White; c.ForeColor = dark ? Color.White : Color.Black; }
        if (c is System.Windows.Forms.Button b) b.FlatAppearance.BorderColor = dark ? Color.FromArgb(145, 171, 201) : Color.FromArgb(80, 105, 135);
    }
    public void SetStatus(string text) { if (!IsDisposed) statusLabel.Text = text; }
    private sealed record MonitorChoice(string Id, string Name);
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
