// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
using System.Drawing;
using System.Runtime.InteropServices;

namespace OpenOLEDShifter;

// Physical-pixel monitor discovery, isolated from the shared Avalonia UI.
internal sealed record WindowsDisplay(string DeviceName, Rectangle Bounds, Rectangle WorkingArea, bool Primary)
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct MonitorInfo
    {
        public int Size;
        public Native.Rect Monitor, Work;
        public uint Flags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Device;
    }
    private delegate bool MonitorCallback(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorCallback callback, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    private static WindowsDisplay Read(IntPtr monitor)
    {
        var info = new MonitorInfo { Size = Marshal.SizeOf<MonitorInfo>(), Device = "" };
        if (!GetMonitorInfo(monitor, ref info)) throw new InvalidOperationException("无法读取显示器信息。");
        return new(info.Device, info.Monitor.Value, info.Work.Value, (info.Flags & 1) != 0);
    }
    public static WindowsDisplay[] AllScreens
    {
        get
        {
            var result = new List<WindowsDisplay>();
            if (!EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (monitor, _, _, _) => { result.Add(Read(monitor)); return true; }, IntPtr.Zero))
                throw new InvalidOperationException("无法枚举显示器。");
            return result.ToArray();
        }
    }
    public static WindowsDisplay? PrimaryScreen => AllScreens.FirstOrDefault(s => s.Primary);
    public static WindowsDisplay FromHandle(IntPtr hwnd) => Read(MonitorFromWindow(hwnd, 2));
}
