// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using System.Diagnostics;
using System.Text.Json;
using Avalonia.Threading;

namespace OpenOLEDShifter;

internal static class Program
{
    private sealed record SessionState(DesktopSettings Settings, string Status);

    [STAThread]
    private static int Main(string[] args) => DesktopApp.Run(args, desktop =>
    {
        bool smoke = args.Contains("--smoke-test");
        bool loaded = false, sending = false, closed = false;
        SettingsWindow window = null!;
        window = new SettingsWindow(null,
            async settings => await Send("Apply", settings),
            async () => await Send("Pause"),
            async () => await Send("Shift"));
        desktop.MainWindow = window;
        desktop.ShutdownMode = Avalonia.Controls.ShutdownMode.OnMainWindowClose;
        var refresh = new DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
        window.SetStatus("正在连接 GNOME 扩展…");
        window.Closed += (_, _) =>
        {
            closed = true;
            refresh.Stop();
        };
        window.Show();
        if (smoke)
        {
            window.LoadSettings(new DesktopSettings(ShiftPolicy.DefaultInterval, ShiftPolicy.DefaultRadius, false, false));
            window.SetStatus("壁纸位移已暂停");
            window.SetAvailable(true);
            DesktopApp.SmokeTest(window, desktop);
            return;
        }

        refresh.Tick += async (_, _) => await Refresh();
        refresh.Start();
        _ = Refresh();

        async Task Refresh()
        {
            if (sending || closed) return;
            try { await Send("GetState"); }
            catch (Exception) { /* The window shows the connection error and retries. */ }
        }

        async Task Send(string method, DesktopSettings? settings = null)
        {
            if (sending)
            {
                // A refresh is read-only. Let an explicit edit wait for it.
                do { await Task.Delay(25); } while (sending && !closed);
            }
            if (closed) return;
            sending = true;
            try
            {
                var session = await CallExtension(method, settings);
                if (closed) return;
                if (!loaded || method == "Apply")
                    window.LoadSettings(session.Settings);
                loaded = true;
                window.SetStatus(session.Status);
                window.SetAvailable(true);
            }
            catch (Exception error)
            {
                if (closed) return;
                loaded = false;
                window.SetAvailable(false);
                window.SetStatus("无法连接 OpenOLEDShifter GNOME 扩展。请确认扩展已启用。");
                throw new InvalidOperationException(error.Message, error);
            }
            finally { sending = false; }
        }
    });

    private static async Task<SessionState> CallExtension(string method, DesktopSettings? settings)
    {
        var start = new ProcessStartInfo("gjs") { RedirectStandardOutput = true, RedirectStandardError = true };
        start.ArgumentList.Add("-m");
        start.ArgumentList.Add(Path.Combine(AppContext.BaseDirectory, "bridge.js"));
        start.ArgumentList.Add(method);
        if (settings is not null)
        {
            start.ArgumentList.Add(settings.IntervalSeconds.ToString(System.Globalization.CultureInfo.InvariantCulture));
            start.ArgumentList.Add(settings.Radius.ToString(System.Globalization.CultureInfo.InvariantCulture));
            start.ArgumentList.Add(settings.Enabled ? "true" : "false");
            start.ArgumentList.Add(settings.StartAtLogin ? "true" : "false");
        }
        using var process = Process.Start(start) ?? throw new InvalidOperationException("无法启动 GJS。");
        var output = process.StandardOutput.ReadToEndAsync();
        var errors = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        if (process.ExitCode != 0)
            throw new InvalidOperationException((await errors).Trim());
        return JsonSerializer.Deserialize<SessionState>(await output)
            ?? throw new InvalidOperationException("GNOME 扩展返回了空状态。");
    }
}
