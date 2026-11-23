// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
using System.Diagnostics;
using System.Text.Json;

namespace OpenOLEDShifter;

internal sealed class GnomeBackend : IDesktopBackend
{
    public DesktopCapabilities Capabilities { get; } = new("Ubuntu · GNOME 壁纸", DesktopFeatures.None,
        "支持 GNOME Wayland 单屏整数缩放。位移由 GNOME 扩展在后台执行；桌面图标、Dock 和应用窗口暂不支持。");
    public Task<DesktopState> GetStateAsync(CancellationToken token) => Call("GetState", null, token);
    public Task<DesktopState> ApplyAsync(DesktopSettings settings, CancellationToken token) => Call("Apply", settings.Validate(), token);
    public Task<DesktopState> PauseAsync(CancellationToken token) => Call("Pause", null, token);
    public Task<DesktopState> ShiftAsync(CancellationToken token) => Call("Shift", null, token);
    private static async Task<DesktopState> Call(string method, DesktopSettings? settings, CancellationToken token)
    {
        var start = new ProcessStartInfo("gjs")
        { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
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
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        using var process = Process.Start(start) ?? throw new InvalidOperationException("无法启动 GJS。");
        var output = process.StandardOutput.ReadToEndAsync();
        var errors = process.StandardError.ReadToEndAsync();
        try { await process.WaitForExitAsync(timeout.Token); }
        catch (OperationCanceledException)
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            await process.WaitForExitAsync();
            await Task.WhenAll(output, errors);
            token.ThrowIfCancellationRequested();
            throw new TimeoutException("GNOME 扩展连接超时，请确认扩展已启用。");
        }
        string result = await output, error = await errors;
        if (process.ExitCode != 0) throw new InvalidOperationException(error.Trim());
        return JsonSerializer.Deserialize<DesktopState>(result)
            ?? throw new InvalidOperationException("GNOME 扩展返回了空状态。");
    }
}
