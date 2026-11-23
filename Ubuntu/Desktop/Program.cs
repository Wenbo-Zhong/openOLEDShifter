// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
using Avalonia.Controls;

namespace OpenOLEDShifter;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args) => DesktopApp.Run(args, desktop =>
    {
        bool smoke = args.Contains("--smoke-test");
        var session = new SettingsSession(new GnomeBackend(), smoke);
        desktop.MainWindow = session.Window;
        desktop.ShutdownMode = ShutdownMode.OnMainWindowClose;
        desktop.Exit += (_, _) => session.Dispose();
        session.Show();
        if (smoke) DesktopApp.SmokeTest(session.Window, desktop);
    });
}
