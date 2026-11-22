// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace OpenOLEDShifter;

internal sealed class WindowState
{
    public long Handle { get; set; }
    public int Pid { get; set; }
    public long StartTime { get; set; }
    public Point Origin { get; set; }
    public Point Last { get; set; }
    public Size Size { get; set; }
    public bool Desktop { get; set; }
    public bool Fse { get; set; }
    public Point? NormalOrigin { get; set; }
}
internal sealed class WallpaperState
{
    public string Monitor { get; set; } = "";
    public string Original { get; set; } = "";
    public string Last { get; set; } = "";
}
internal sealed class RecoveryState
{
    public List<WindowState> Windows { get; set; } = [];
    public List<WallpaperState> Wallpapers { get; set; } = [];
    public int? TaskbarOriginal { get; set; }
    public int? TaskbarLast { get; set; }
    public static RecoveryState Load(string path)
    {
        if (!File.Exists(path)) return new();
        // Fail closed on a malformed journal: never overwrite the only recovery copy.
        return System.Text.Json.JsonSerializer.Deserialize<RecoveryState>(File.ReadAllText(path))
            ?? throw new InvalidDataException("恢复记录为空，请保留 recovery.json 并检查。 ");
    }
}

internal sealed class ShiftEngine
{
    private readonly string journal;
    private RecoveryState state;
    private int step;
    public Point Offset { get; private set; }
    public string Detail { get; private set; } = "等待位移";
    public ShiftEngine(string? journalPath = null) { journal = journalPath ?? Path.Combine(Program.DataDir, "recovery.json"); state = RecoveryState.Load(journal); }
    internal void ShiftFixture(IntPtr handle, Point offset) => ShiftWindow(handle, false, offset);
    private void Save() => Store.Write(journal, state);

    public void Shift(Preferences settings)
    {
        var orbit = Orbit.Points(settings.Radius);
        step = (step + 1) % (2 * (orbit.Count - 1));
        Offset = orbit[Orbit.PingPong(step, orbit.Count)];
        List<string> errors = [];
        if (settings.Wallpaper) Attempt(() => ShiftWallpapers(settings), errors);
        if (settings.Icons) Attempt(() => ShiftWindow(Native.DesktopView(), true, Offset), errors);
        if (settings.AutoHideTaskbar) Attempt(() =>
        {
            int current = Native.TaskbarState();
            if (state.TaskbarOriginal == null)
            {
                state.TaskbarOriginal = current;
                state.TaskbarLast = current | 1;
                Save(); Native.TaskbarState(current | 1);
            }
        }, errors);
        Attempt(() => ShiftAllowedWindows(settings), errors);
        Detail = errors.Count == 0 ? $"偏移 {Offset.X:+0;-0;0}, {Offset.Y:+0;-0;0} 像素" : string.Join("；", errors.Distinct());
    }
    private static void Attempt(Action action, List<string> errors)
    {
        try { action(); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or COMException or InvalidOperationException or ArgumentException or System.ComponentModel.Win32Exception)
        { errors.Add(e.Message); Program.Log(e.ToString()); }
    }
    private static bool Identity(WindowState s)
    {
        IntPtr h = (IntPtr)s.Handle;
        if (!Native.IsWindow(h)) return false;
        Native.GetWindowThreadProcessId(h, out uint pid);
        if (pid != s.Pid) return false;
        try { using var p = System.Diagnostics.Process.GetProcessById((int)pid); return p.StartTime.ToUniversalTime().Ticks == s.StartTime; }
        catch (Exception e) when (e is ArgumentException or System.ComponentModel.Win32Exception or InvalidOperationException) { return false; }
    }
    private void ShiftWindow(IntPtr h, bool desktop, Point offset, bool fse = false)
    {
        if (h == IntPtr.Zero || !Native.IsWindow(h) || !Native.IsWindowVisible(h) || Native.IsIconic(h)) return;
        Rectangle rect = Native.BoundsInParent(h);
        var existing = state.Windows.FirstOrDefault(x => x.Handle == h.ToInt64());
        if (existing != null && (!Identity(existing) || rect.Location != existing.Last || rect.Size != existing.Size))
        {
            // User, Explorer or another app moved it. Adopt its new position instead of undoing it.
            state.Windows.Remove(existing); existing = null;
        }
        if (existing == null)
        {
            Native.GetWindowThreadProcessId(h, out uint pid);
            using var process = System.Diagnostics.Process.GetProcessById((int)pid);
            existing = new WindowState { Handle = h.ToInt64(), Pid = (int)pid, StartTime = process.StartTime.ToUniversalTime().Ticks,
                Origin = rect.Location, Last = rect.Location, Size = rect.Size, Desktop = desktop, Fse = fse };
            if (!desktop && Native.NormalPlacement(h, out var placement)) existing.NormalOrigin = new Point(placement.Normal.Left, placement.Normal.Top);
            state.Windows.Add(existing);
        }
        Point target = existing.Origin + (Size)offset;
        if (!desktop && !fse)
        {
            Rectangle area = Screen.FromHandle(h).WorkingArea;
            target.X = Math.Clamp(target.X, area.Left - settingsMargin, Math.Max(area.Left - settingsMargin, area.Right - rect.Width + settingsMargin));
            target.Y = Math.Clamp(target.Y, area.Top - settingsMargin, Math.Max(area.Top - settingsMargin, area.Bottom - rect.Height + settingsMargin));
        }
        Point previous = existing.Last;
        existing.Last = target; Save(); // durable intent before changing another window
        if (!Native.Move(h, target))
        {
            existing.Last = previous; Save();
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "窗口位移失败（可能权限不足）");
        }
    }
    private const int settingsMargin = 8;
    private void ShiftAllowedWindows(Preferences settings)
    {
        IntPtr foreground = Native.GetForegroundWindow();
        if (settings.FseWindows && Native.IsVendorFseWindow(foreground) && Native.Fullscreen(settings.Monitor))
            ShiftWindow(foreground, false, Orbit.Points(1)[Orbit.PingPong(step, 9)], true);
        var names = settings.WindowProcesses.Split([',', ';', '\n'], StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
            .Select(x => Path.GetFileNameWithoutExtension(x)).ToHashSet(StringComparer.OrdinalIgnoreCase);
        Native.EnumWindows((h, _) =>
        {
            string cls = Native.Class(h), title = Native.Title(h);
            bool vendorDemo = cls == "ApplicationFrameWindow" && title is "Microsoft Retail Demo" or "Intelligent Point of Sale";
            if (!vendorDemo && (names.Count == 0 || !names.Contains(Native.ProcessName(h)))) return true;
            if (!Native.IsWindowVisible(h) || Native.IsIconic(h) || Native.IsZoomed(h) || Native.IsCloaked(h)) return true;
            if (settings.Monitor != "*" && Screen.FromHandle(h).DeviceName != settings.Monitor) return true;
            if (Native.ProcessName(h) == "OpenOLEDShifter") return true;
            ShiftWindow(h, false, Offset); return true;
        }, IntPtr.Zero);
    }
    public void RestoreInactiveFse()
    {
        foreach (var item in state.Windows.Where(x => x.Fse && (IntPtr)x.Handle != Native.GetForegroundWindow()).ToArray())
        {
            if (Identity(item) && Native.BoundsInParent((IntPtr)item.Handle).Location == item.Last)
                if (!Native.Move((IntPtr)item.Handle, item.Origin)) continue;
            state.Windows.Remove(item); Save();
        }
    }
    private void ShiftWallpapers(Preferences settings)
    {
        using var api = new WallpaperApi();
        api.Api.GetStatus(out int status);
        api.Api.GetPosition(out int position);
        if ((status & 2) != 0 || position is 1 or 5)
            throw new InvalidOperationException("平铺、跨屏或幻灯片壁纸暂不位移；图标等功能仍可用");
        api.Api.GetMonitorDevicePathCount(out uint count);
        api.Api.GetBackgroundColor(out uint bg);
        for (uint i = 0; i < count; i++)
        {
            api.Api.GetMonitorDevicePathAt(i, out string id);
            api.Api.GetMonitorRECT(id, out var monitorRect);
            Rectangle bounds = monitorRect.Value;
            if (bounds.Width <= 0 || bounds.Height <= 0) continue;
            var screen = Screen.AllScreens.FirstOrDefault(s => s.Bounds == bounds);
            if (screen == null || (settings.Monitor != "*" && screen.DeviceName != settings.Monitor)) continue;
            api.Api.GetWallpaper(id, out string current);
            var record = state.Wallpapers.FirstOrDefault(s => s.Monitor == id);
            if (record != null && !string.Equals(record.Last, current, StringComparison.OrdinalIgnoreCase))
            { state.Wallpapers.Remove(record); record = null; }
            string original = record?.Original ?? current;
            if (string.IsNullOrWhiteSpace(original) || !File.Exists(original)) continue;
            string cache = Path.Combine(Program.DataDir, "wallpapers"); Directory.CreateDirectory(cache);
            string output = Path.Combine(cache, $"monitor-{i}-{Orbit.PingPong(step, Orbit.Points(settings.Radius).Count)}.png");
            using (var input = Image.FromFile(original))
            using (var bitmap = WallpaperRenderer.Render(input, bounds.Size, position, Color.FromArgb((int)(bg & 255), (int)((bg >> 8) & 255), (int)((bg >> 16) & 255)), Offset))
            {
                string temp = output + ".tmp"; bitmap.Save(temp, ImageFormat.Png); File.Move(temp, output, true);
            }
            if (record == null) { record = new WallpaperState { Monitor = id, Original = original }; state.Wallpapers.Add(record); }
            record.Last = output; Save();
            api.Api.SetWallpaper(id, output);
        }
    }
    public bool Restore()
    {
        var errors = new List<string>();
        foreach (var item in state.Windows.ToArray())
            Attempt(() =>
            {
                if (Identity(item))
                {
                    IntPtr h = (IntPtr)item.Handle;
                    if (Native.IsIconic(h) && item.NormalOrigin is Point normal)
                    {
                        var expected = normal + (Size)(item.Last - (Size)item.Origin);
                        if (!Native.RestoreMinimized(h, normal, expected)) throw new InvalidOperationException("最小化窗口恢复失败");
                    }
                    else
                    {
                        Rectangle current = Native.BoundsInParent(h);
                        if (current.Location == item.Last && current.Size == item.Size && !Native.IsIconic(h))
                        if (!Native.Move((IntPtr)item.Handle, item.Origin)) throw new InvalidOperationException("窗口原位恢复失败");
                    }
                }
                state.Windows.Remove(item);
            }, errors);
        if (state.Wallpapers.Count > 0)
            Attempt(() =>
            {
                using var api = new WallpaperApi();
                foreach (var item in state.Wallpapers.ToArray())
                {
                    api.Api.GetWallpaper(item.Monitor, out string current);
                    if (string.Equals(current, item.Last, StringComparison.OrdinalIgnoreCase))
                    {
                        if (!File.Exists(item.Original)) throw new IOException("原壁纸文件已不存在；已保留恢复记录");
                        api.Api.SetWallpaper(item.Monitor, item.Original);
                    }
                    state.Wallpapers.Remove(item);
                }
            }, errors);
        if (state.TaskbarOriginal != null)
            Attempt(() =>
            {
                if (Native.TaskbarState() == state.TaskbarLast) Native.TaskbarState(state.TaskbarOriginal.Value);
                state.TaskbarOriginal = state.TaskbarLast = null;
            }, errors);
        Save(); step = 0; Offset = Point.Empty;
        Detail = errors.Count == 0 ? "已恢复原位" : "恢复未完成：" + string.Join("；", errors);
        return errors.Count == 0;
    }
}

internal static class WallpaperRenderer
{
    public static Bitmap Render(Image source, Size size, int mode, Color background, Point offset)
    {
        using var canvas = new Bitmap(size.Width, size.Height, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(canvas))
        {
            g.Clear(background); g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            double factor = mode switch { 2 => 0, 3 => Math.Min((double)size.Width / source.Width, (double)size.Height / source.Height),
                4 => Math.Max((double)size.Width / source.Width, (double)size.Height / source.Height), _ => 1 };
            int width = mode == 2 ? size.Width : (int)Math.Round(source.Width * factor);
            int height = mode == 2 ? size.Height : (int)Math.Round(source.Height * factor);
            using var attributes = new ImageAttributes();
            attributes.SetWrapMode(WrapMode.TileFlipXY);
            g.DrawImage(source, new Rectangle((size.Width - width) / 2, (size.Height - height) / 2, width, height),
                0, 0, source.Width, source.Height, GraphicsUnit.Pixel, attributes);
        }
        return Translate(canvas, offset);
    }
    public static Bitmap Translate(Bitmap canvas, Point offset)
    {
        var output = new Bitmap(canvas.Width, canvas.Height, PixelFormat.Format32bppArgb);
        using var g = Graphics.FromImage(output);
        g.CompositingMode = CompositingMode.SourceCopy;
        // Wrap at the edges; no blank strip and no repeated rescaling on each tick.
        int x = ((offset.X % canvas.Width) + canvas.Width) % canvas.Width;
        int y = ((offset.Y % canvas.Height) + canvas.Height) % canvas.Height;
        for (int dx = -1; dx <= 0; dx++) for (int dy = -1; dy <= 0; dy++)
            g.DrawImageUnscaled(canvas, x + dx * canvas.Width, y + dy * canvas.Height);
        return output;
    }
}
