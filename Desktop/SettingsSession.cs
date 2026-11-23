// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
using Avalonia.Controls;
using Avalonia.Threading;

namespace OpenOLEDShifter;

// Both platforms use the same command serialization, editing and connection UI.
public sealed class SettingsSession : IDisposable
{
    private readonly SettingsWindow window;
    private readonly BackendConnection connection;
    private readonly CancellationTokenSource lifetime = new();
    private readonly DispatcherTimer refresh = new() { Interval = TimeSpan.FromSeconds(3) };
    private bool disposed, refreshing;
    public Window Window => window;
    public SettingsSession(IDesktopBackend backend, bool smoke = false)
    {
        connection = new BackendConnection(backend);
        window = new SettingsWindow(backend.Capabilities,
            settings => Send(t => connection.ApplyAsync(settings, t), true),
            () => Send(connection.PauseAsync, true),
            () => Send(connection.ShiftAsync, false));
        window.Closed += (_, _) => Dispose();
        if (smoke)
        {
            window.LoadSettings(new DesktopSettings());
            window.SetStatus("界面预览 · 实际位移已禁用");
            window.SetAvailable(false);
            return;
        }
        window.SetStatus("正在连接位移后端…");
        refresh.Tick += async (_, _) => await Refresh();
        refresh.Start(); _ = Refresh();
    }
    public void Show() { window.Show(); window.WindowState = WindowState.Normal; window.Activate(); }
    private async Task Refresh()
    {
        if (disposed || refreshing) return;
        refreshing = true;
        try { await Send(connection.RefreshAsync, false); }
        catch (Exception) { /* Connection feedback is shown by Send; polling can reconnect. */ }
        finally { refreshing = false; }
    }
    private async Task Send(Func<CancellationToken, Task<DesktopState>> action, bool explicitEdit)
    {
        if (disposed) return;
        try
        {
            var state = await action(lifetime.Token);
            if (disposed) return;
            if (explicitEdit || !window.IsDirty) window.LoadSettings(state.Settings);
            window.SetStatus(state.Status); window.SetAvailable(true);
        }
        catch (OperationCanceledException) when (disposed) { }
        catch (Exception error)
        {
            if (disposed) return;
            window.SetAvailable(false);
            window.SetStatus("位移后端暂不可用：" + error.Message);
            throw;
        }
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true; refresh.Stop(); lifetime.Cancel(); lifetime.Dispose();
    }
}
