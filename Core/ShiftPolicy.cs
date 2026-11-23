// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

namespace OpenOLEDShifter;

internal readonly record struct PixelOffset(int X, int Y);

internal static class ShiftPolicy
{
    public const int DefaultInterval = 200;
    public const int MinInterval = 10;
    public const int MaxInterval = 86400;
    public const int DefaultRadius = 1;
    public const int MinRadius = 1;
    public const int MaxRadius = 8;

    public static bool StillOwned<T>(T current, T last) =>
        EqualityComparer<T>.Default.Equals(current, last);

    // A square spiral: origin, right, down, left, up; then expand one ring.
    public static List<PixelOffset> Points(int radius)
    {
        if (radius is < MinRadius or > MaxRadius) throw new ArgumentOutOfRangeException(nameof(radius));
        var result = new List<PixelOffset> { new(0, 0) };
        int x = 0, y = 0, direction = 0, length = 1;
        PixelOffset[] dirs = [new(1, 0), new(0, 1), new(-1, 0), new(0, -1)];
        int count = (radius * 2 + 1) * (radius * 2 + 1);
        while (result.Count < count)
        {
            for (int repeat = 0; repeat < 2; repeat++)
            {
                PixelOffset d = dirs[direction++ % 4];
                for (int step = 0; step < length && result.Count < count; step++)
                { x += d.X; y += d.Y; result.Add(new PixelOffset(x, y)); }
            }
            length++;
        }
        return result;
    }
    public static int PingPong(int step, int count)
    {
        if (count <= 1) return 0;
        int period = 2 * (count - 1), n = ((step % period) + period) % period;
        return n < count ? n : period - n;
    }
}
