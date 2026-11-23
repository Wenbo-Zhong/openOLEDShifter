// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media.Imaging;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;

namespace OpenOLEDShifter;

internal sealed class DesktopApp(Action<IClassicDesktopStyleApplicationLifetime> initialize) : Application
{
    public static readonly Uri IconUri = new("avares://OpenOLEDShifter/Assets/app-icon.png");

    public static int Run(string[] args, Action<IClassicDesktopStyleApplicationLifetime> initialize) =>
        AppBuilder.Configure(() => new DesktopApp(initialize)).UsePlatformDetect()
            .StartWithClassicDesktopLifetime(args, ShutdownMode.OnExplicitShutdown);

    public override void Initialize() => Styles.Add(new FluentTheme());

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            initialize(desktop);
        base.OnFrameworkInitializationCompleted();
    }

    public static void SmokeTest(Window window, IClassicDesktopStyleApplicationLifetime desktop)
    {
        DispatcherTimer.RunOnce(() =>
        {
            try
            {
                Directory.CreateDirectory("artifacts");
                double scale = window.RenderScaling;
                var pixels = new PixelSize((int)Math.Ceiling(window.Bounds.Width * scale),
                    (int)Math.Ceiling(window.Bounds.Height * scale));
                using var image = new RenderTargetBitmap(pixels, new Vector(96 * scale, 96 * scale));
                image.Render(window);
                image.Save("artifacts/settings-preview.png", PngBitmapEncoderOptions.Default);
                File.WriteAllText("artifacts/smoke-test.txt", $"Avalonia settings rendered at {scale:P0}; no desktop mutation.\n");
                desktop.Shutdown();
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error);
                desktop.Shutdown(1);
            }
        }, TimeSpan.FromMilliseconds(800));
    }
}
