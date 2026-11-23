// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform;

namespace OpenOLEDShifter;

internal sealed class SettingsWindow : Window
{
    private readonly TextBlock status = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock feedback = new() { TextWrapping = TextWrapping.Wrap, IsVisible = false };
    private readonly CheckBox enabled = new() { Content = "启用定时位移" };
    private readonly CheckBox startup = new() { Content = "登录后自动开始" };
    private readonly CheckBox wallpaper = new() { Content = "位移静态壁纸" };
    private readonly CheckBox icons = new() { Content = "位移桌面图标层（所有屏幕）" };
    private readonly CheckBox taskbar = new() { Content = "运行时自动隐藏任务栏" };
    private readonly CheckBox fullscreen = new() { Content = "全屏应用运行时暂停并还原" };
    private readonly CheckBox fse = new() { Content = "位移 Xbox / 掌机界面中的白名单窗口" };
    private readonly NumericUpDown interval = Number(ShiftPolicy.MinInterval, ShiftPolicy.MaxInterval);
    private readonly NumericUpDown radius = Number(ShiftPolicy.MinRadius, ShiftPolicy.MaxRadius);
    private readonly ComboBox monitors = new() { MinWidth = 220 };
    private readonly TextBox processes = new() { PlaceholderText = "例如 notepad.exe，多个进程用逗号分隔" };
    private readonly Button save = new() { Content = "保存设置" };
    private readonly Button pause = new() { Content = "暂停并还原" };
    private readonly Button shift = new() { Content = "立即位移" };
    private readonly StackPanel fields = new() { Spacing = 9 };
    private readonly DesktopCapabilities capabilities;
    private bool busy, available;
    private bool loading;
    public bool IsDirty { get; private set; }

    public SettingsWindow(DesktopCapabilities capabilities,
        Func<DesktopSettings, Task> apply, Func<Task> restore, Func<Task> shiftOnce)
    {
        this.capabilities = capabilities;
        var monitorChoices = capabilities.Monitors;
        bool windows = capabilities.Features.HasFlag(DesktopFeatures.MonitorSelection);
        Title = "OpenOLEDShifter · OLED 像素位移";
        Width = 620;
        Height = windows ? 810 : 460;
        MinWidth = 380;
        MinHeight = 340;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        using (var icon = AssetLoader.Open(DesktopApp.IconUri))
            Icon = new WindowIcon(icon);

        var heading = new TextBlock { Text = "OLED 像素位移", FontSize = 26, FontWeight = FontWeight.SemiBold };
        var platform = new TextBlock
        {
            Text = capabilities.Name,
            Opacity = 0.72,
        };
        var statusPanel = new Border
        {
            Background = new SolidColorBrush(Color.FromArgb(26, 75, 140, 225)),
            CornerRadius = new CornerRadius(8),
            Padding = new Thickness(14),
            Child = status,
        };
        fields.Children.Add(enabled);
        fields.Children.Add(Row("位移间隔（秒）", interval));
        fields.Children.Add(Row("最大偏移（物理像素）", radius));
        fields.Children.Add(startup);

        foreach (var (feature, option) in new[] {
            (DesktopFeatures.Wallpaper, wallpaper), (DesktopFeatures.Icons, icons),
            (DesktopFeatures.AutoHideTaskbar, taskbar), (DesktopFeatures.PauseFullscreen, fullscreen),
            (DesktopFeatures.FseWindows, fse) })
            if (capabilities.Features.HasFlag(feature)) fields.Children.Add(option);
        if (windows && monitorChoices is not null)
        {
            monitors.ItemsSource = monitorChoices.Select(choice => new ComboBoxItem
            {
                Content = choice.Name,
                Tag = choice.Id,
            }).ToArray();
            fields.Children.Add(Row("壁纸 / 窗口目标屏幕", monitors));
        }
        if (capabilities.Features.HasFlag(DesktopFeatures.WindowProcesses))
        {
            fields.Children.Add(new TextBlock { Text = "额外位移的应用进程" });
            fields.Children.Add(processes);
        }

        save.Classes.Add("accent");
        var actions = new WrapPanel { Orientation = Orientation.Horizontal };
        foreach (var button in new[] { save, pause, shift })
        {
            button.Margin = new Thickness(0, 0, 8, 6);
            actions.Children.Add(button);
        }

        var body = new StackPanel { Margin = new Thickness(24), Spacing = 12 };
        foreach (var item in new Control[] { heading, platform, statusPanel, fields, actions, feedback })
            body.Children.Add(item);
        if (!string.IsNullOrEmpty(capabilities.Description))
        {
            body.Children.Add(new TextBlock
            {
                Text = capabilities.Description,
                Opacity = 0.72,
                TextWrapping = TextWrapping.Wrap,
            });
        }
        Content = new ScrollViewer { Content = body };

        save.Click += async (_, _) => await Run(async () => await apply(ReadSettings()), "设置已保存");
        pause.Click += async (_, _) => await Run(async () =>
        {
            await restore();
            enabled.IsChecked = false;
        }, null);
        shift.Click += async (_, _) => await Run(shiftOnce, null);
        foreach (var field in new[] { enabled, startup, wallpaper, icons, taskbar, fullscreen, fse })
            field.IsCheckedChanged += (_, _) => Dirty();
        interval.ValueChanged += (_, _) => Dirty();
        radius.ValueChanged += (_, _) => Dirty();
        processes.TextChanged += (_, _) => Dirty();
        monitors.SelectionChanged += (_, _) => Dirty();
        SetAvailable(false);
    }
    private void Dirty() { if (!loading) IsDirty = true; }

    public void LoadSettings(DesktopSettings settings)
    {
        loading = true;
        interval.Value = settings.IntervalSeconds;
        radius.Value = settings.Radius;
        enabled.IsChecked = settings.Enabled;
        startup.IsChecked = settings.StartAtLogin;
        wallpaper.IsChecked = settings.Wallpaper;
        icons.IsChecked = settings.Icons;
        taskbar.IsChecked = settings.AutoHideTaskbar;
        fullscreen.IsChecked = settings.PauseFullscreen;
        fse.IsChecked = settings.FseWindows;
        processes.Text = settings.WindowProcesses;
        if (capabilities.Features.HasFlag(DesktopFeatures.MonitorSelection) && monitors.Items.Count > 0)
        {
            monitors.SelectedItem = monitors.Items.OfType<ComboBoxItem>()
                .FirstOrDefault(item => Equals(item.Tag, settings.Monitor)) ?? monitors.Items[0];
        }
        loading = false;
        IsDirty = false;
    }

    public void SetStatus(string value) => status.Text = value;

    public void SetAvailable(bool value)
    {
        available = value;
        fields.IsEnabled = save.IsEnabled = pause.IsEnabled = shift.IsEnabled = available && !busy;
    }

    private DesktopSettings ReadSettings() => new(
        ReadNumber(interval, "位移间隔", ShiftPolicy.MinInterval, ShiftPolicy.MaxInterval),
        ReadNumber(radius, "最大偏移", ShiftPolicy.MinRadius, ShiftPolicy.MaxRadius),
        enabled.IsChecked == true, startup.IsChecked == true,
        wallpaper.IsChecked == true, icons.IsChecked == true, taskbar.IsChecked == true,
        fullscreen.IsChecked == true, fse.IsChecked == true,
        (monitors.SelectedItem as ComboBoxItem)?.Tag as string ?? "", processes.Text ?? "");

    private async Task Run(Func<Task> action, string? success)
    {
        busy = true;
        SetAvailable(available);
        try
        {
            await action();
            feedback.Text = success;
            feedback.IsVisible = success is not null;
        }
        catch (Exception error)
        {
            feedback.Text = error.Message;
            feedback.IsVisible = true;
        }
        finally
        {
            busy = false;
            SetAvailable(available);
        }
    }

    private static NumericUpDown Number(int min, int max) => new()
    {
        Minimum = min, Maximum = max, Increment = 1, FormatString = "0", Width = 132,
    };

    private static int ReadNumber(NumericUpDown field, string label, int minimum, int maximum)
    {
        if (!int.TryParse(field.Text, out int value) || value < minimum || value > maximum)
            throw new InvalidOperationException($"{label}须为 {minimum}–{maximum} 之间的整数。");
        return value;
    }

    private static Control Row(string label, Control control)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 14 };
        row.Children.Add(new TextBlock { Text = label, Width = 160, VerticalAlignment = VerticalAlignment.Center });
        row.Children.Add(control);
        return row;
    }
}
