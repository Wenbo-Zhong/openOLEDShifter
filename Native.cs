// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using System.Drawing;
using System.Windows.Forms;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace OpenOLEDShifter;

internal static class Native
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBoxW(IntPtr owner, string text, string caption, uint type);

    internal static void ShowMessage(string text) => MessageBoxW(IntPtr.Zero, text, "OpenOLEDShifter", 0);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; public readonly Rectangle Value => Rectangle.FromLTRB(Left, Top, Right, Bottom); }
    [StructLayout(LayoutKind.Sequential)]
    private struct AppBar { public uint Size; public IntPtr Hwnd; public uint Callback, Edge; public Rect Rect; public IntPtr Param; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Placement { public uint Length, Flags, Show; public Point Min, Max; public Rect Normal; }
    [DllImport("user32.dll")] private static extern bool GetWindowPlacement(IntPtr hwnd, ref Placement placement);
    [DllImport("user32.dll")] private static extern bool SetWindowPlacement(IntPtr hwnd, ref Placement placement);
    public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr param);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string? cls, string? title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? cls, string? title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder text, int length);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int length);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern int MapWindowPoints(IntPtr from, IntPtr to, ref Point point, uint count);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("shell32.dll")] private static extern UIntPtr SHAppBarMessage(uint message, ref AppBar data);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern int GetApplicationUserModelId(IntPtr process, ref uint length, StringBuilder id);
    public static string Class(IntPtr hwnd) { var s = new StringBuilder(256); GetClassName(hwnd, s, s.Capacity); return s.ToString(); }
    public static string Title(IntPtr hwnd) { var s = new StringBuilder(512); GetWindowText(hwnd, s, s.Capacity); return s.ToString(); }
    public static string ProcessName(IntPtr hwnd)
    { try { GetWindowThreadProcessId(hwnd, out uint pid); using var p = Process.GetProcessById((int)pid); return p.ProcessName; }
      catch (Exception e) when (e is ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception) { return ""; } }
    public static bool IsCloaked(IntPtr hwnd) => DwmGetWindowAttribute(hwnd, 14, out int v, 4) == 0 && v != 0;
    public static bool VendorRunning()
    {
        var all = Process.GetProcessesByName("AsusOLEDShifter");
        try { return all.Length != 0; } finally { foreach (var p in all) p.Dispose(); }
    }
    public static bool IsVendorFseWindow(IntPtr hwnd)
    {
        string cls = Class(hwnd), processName = ProcessName(hwnd);
        if (cls == "XamlExplorerHostIslandWindow" && processName.Equals("explorer", StringComparison.OrdinalIgnoreCase)) return true;
        if (cls != "ApplicationFrameWindow") return false;
        try
        {
            GetWindowThreadProcessId(hwnd, out uint pid);
            using var process = Process.GetProcessById((int)pid);
            uint length = 512; var id = new StringBuilder((int)length);
            if (GetApplicationUserModelId(process.Handle, ref length, id) != 0) return false;
            return id.ToString() is "Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App"
                or "Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.AppL"
                or "B9ECED6F.ArmouryCrateSE_qmba6cd70vzyy!App";
        }
        catch (Exception e) when (e is ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception) { return false; }
    }
    public static bool Fullscreen(string device, bool allowFse = false)
    {
        IntPtr h = GetForegroundWindow();
        if (h == IntPtr.Zero || !IsWindowVisible(h) || IsIconic(h) || IsCloaked(h)) return false;
        string cls = Class(h);
        if (cls is "Progman" or "WorkerW" or "Shell_TrayWnd" or "Shell_SecondaryTrayWnd") return false;
        if (allowFse && IsVendorFseWindow(h)) return false;
        if (!GetWindowRect(h, out var r)) return false;
        Screen screen = Screen.FromHandle(h);
        if (device != "*" && screen.DeviceName != device) return false;
        Rectangle b = screen.Bounds;
        // Tolerate the one-pixel displacement of a previously shifted full-screen shell.
        return r.Left <= b.Left + 2 && r.Top <= b.Top + 2 && r.Right >= b.Right - 2 && r.Bottom >= b.Bottom - 2;
    }
    public static IntPtr DesktopView()
    {
        IntPtr view = IntPtr.Zero;
        EnumWindows((h, _) =>
        {
            var def = FindWindowEx(h, IntPtr.Zero, "SHELLDLL_DefView", null);
            if (def == IntPtr.Zero) return true;
            view = FindWindowEx(def, IntPtr.Zero, "SysListView32", null);
            return view == IntPtr.Zero;
        }, IntPtr.Zero);
        return view;
    }
    public static int TaskbarState()
    { var data = new AppBar { Size = (uint)Marshal.SizeOf<AppBar>(), Hwnd = FindWindow("Shell_TrayWnd", null) }; return (int)SHAppBarMessage(4, ref data).ToUInt64(); }
    public static void TaskbarState(int state)
    { var data = new AppBar { Size = (uint)Marshal.SizeOf<AppBar>(), Hwnd = FindWindow("Shell_TrayWnd", null), Param = (IntPtr)state }; SHAppBarMessage(10, ref data); }
    public static Rectangle BoundsInParent(IntPtr h)
    {
        if (!GetWindowRect(h, out var r)) throw new InvalidOperationException("Cannot read window bounds.");
        Point p = new(r.Left, r.Top); MapWindowPoints(IntPtr.Zero, GetParent(h), ref p, 1);
        return new Rectangle(p.X, p.Y, r.Right - r.Left, r.Bottom - r.Top);
    }
    public static bool Move(IntPtr h, Point p) => SetWindowPos(h, IntPtr.Zero, p.X, p.Y, 0, 0, 0x0015); // NOSIZE | NOZORDER | NOACTIVATE
    public static bool NormalPlacement(IntPtr h, out Placement p)
    { p = new Placement { Length = (uint)Marshal.SizeOf<Placement>() }; return GetWindowPlacement(h, ref p); }
    public static bool RestoreMinimized(IntPtr h, Point original, Point expected)
    {
        if (!NormalPlacement(h, out var p)) throw new InvalidOperationException("无法读取最小化窗口恢复位置");
        if (new Point(p.Normal.Left, p.Normal.Top) != expected) return true; // changed elsewhere
        int width = p.Normal.Right - p.Normal.Left, height = p.Normal.Bottom - p.Normal.Top;
        p.Normal = new Rect { Left = original.X, Top = original.Y, Right = original.X + width, Bottom = original.Y + height };
        return SetWindowPlacement(h, ref p);
    }
}

[ComImport, Guid("B92B56A9-8B55-4E14-9A89-0199BBB6F93B"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IDesktopWallpaper
{
    void SetWallpaper([MarshalAs(UnmanagedType.LPWStr)] string monitor, [MarshalAs(UnmanagedType.LPWStr)] string wallpaper);
    void GetWallpaper([MarshalAs(UnmanagedType.LPWStr)] string monitor, [MarshalAs(UnmanagedType.LPWStr)] out string wallpaper);
    void GetMonitorDevicePathAt(uint index, [MarshalAs(UnmanagedType.LPWStr)] out string monitor);
    void GetMonitorDevicePathCount(out uint count);
    void GetMonitorRECT([MarshalAs(UnmanagedType.LPWStr)] string monitor, out Native.Rect rect);
    void SetBackgroundColor(uint color);
    void GetBackgroundColor(out uint color);
    void SetPosition(int position);
    void GetPosition(out int position);
    void SetSlideshow(IntPtr items);
    void GetSlideshow(out IntPtr items);
    void SetSlideshowOptions(int options, uint tick);
    void GetSlideshowOptions(out int options, out uint tick);
    void AdvanceSlideshow([MarshalAs(UnmanagedType.LPWStr)] string monitor, int direction);
    void GetStatus(out int status);
    void Enable([MarshalAs(UnmanagedType.Bool)] bool enabled);
}

internal sealed class WallpaperApi : IDisposable
{
    public IDesktopWallpaper Api { get; } = (IDesktopWallpaper)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("C2CF3110-460E-4FC1-B9D0-8A1C0C9CC4BD"))!)!;
    public void Dispose() => Marshal.FinalReleaseComObject(Api);
}
