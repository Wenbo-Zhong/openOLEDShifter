// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
using System.Drawing;
using System.Runtime.InteropServices;

namespace OpenOLEDShifter;

internal sealed class NativeFixture : IDisposable
{
    private delegate IntPtr WindowProc(IntPtr h, uint message, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WindowClass
    {
        public uint Style; public WindowProc Proc; public int ClassExtra, WindowExtra;
        public IntPtr Instance, Icon, Cursor, Background;
        public string? Menu; public string Name;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct Message { public IntPtr Hwnd; public uint Id; public UIntPtr W; public IntPtr L; public uint Time; public Point Point; public uint Private; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern ushort RegisterClass(ref WindowClass c);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool UnregisterClass(string name, IntPtr instance);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr CreateWindowEx(uint ex, string cls, string title, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr DefWindowProc(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string? name);
    [DllImport("user32.dll")] private static extern bool DestroyWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] private static extern bool PeekMessage(out Message m, IntPtr hwnd, uint min, uint max, uint remove);
    [DllImport("user32.dll")] private static extern bool TranslateMessage(ref Message m);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref Message m);
    private readonly WindowProc proc = DefWindowProc;
    private readonly string className = "OpenOLEDShifterFixture-" + Guid.NewGuid().ToString("N");
    private readonly IntPtr instance = GetModuleHandle(null);
    public IntPtr Handle { get; }
    public NativeFixture()
    {
        var c = new WindowClass { Proc = proc, Name = className, Instance = instance };
        if (RegisterClass(ref c) == 0) throw new InvalidOperationException("Cannot register test window.");
        Handle = CreateWindowEx(0x80, className, "OpenOLEDShifter isolated test fixture", 0x00cf0000,
            100, 100, 320, 200, IntPtr.Zero, IntPtr.Zero, instance, IntPtr.Zero);
        if (Handle == IntPtr.Zero) throw new InvalidOperationException("Cannot create test window.");
    }
    public void Show() => ShowWindow(Handle, 4);
    public void Minimize() => ShowWindow(Handle, 6);
    public void Restore() => ShowWindow(Handle, 9);
    public static void Pump() { while (PeekMessage(out var m, IntPtr.Zero, 0, 0, 1)) { TranslateMessage(ref m); DispatchMessage(ref m); } }
    public void Dispose() { DestroyWindow(Handle); UnregisterClass(className, instance); GC.KeepAlive(proc); }
}
