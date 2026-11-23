// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using System.Text.Json;
using OpenOLEDShifter;

PixelOffset[] expected = [new(0, 0), new(1, 0), new(1, 1), new(0, 1), new(-1, 1), new(-1, 0), new(-1, -1), new(0, -1), new(1, -1)];
Check(ShiftPolicy.Points(ShiftPolicy.DefaultRadius).SequenceEqual(expected));
var cycles = new Dictionary<int, int[][]>();
for (int radius = ShiftPolicy.MinRadius; radius <= ShiftPolicy.MaxRadius; radius++)
{
    var points = ShiftPolicy.Points(radius);
    Check(points.Count == (2 * radius + 1) * (2 * radius + 1));
    Check(points.Distinct().Count() == points.Count);
    Check(points.All(p => Math.Abs(p.X) <= radius && Math.Abs(p.Y) <= radius));
    var frames = ShiftPolicy.Frames(radius);
    var cycle = frames.Select(frame => frame.Offset).ToArray();
    Check(cycle.Take(points.Count).SequenceEqual(points));
    Check(cycle.Skip(points.Count).SequenceEqual(points.Skip(1).SkipLast(1).Reverse()));
    Check(cycle.Zip(cycle.Skip(1).Append(cycle[0])).All(pair =>
        Math.Abs(pair.First.X - pair.Second.X) + Math.Abs(pair.First.Y - pair.Second.Y) == 1));
    cycles.Add(radius, frames.Select(frame => new[] { frame.Offset.X, frame.Offset.Y, frame.NextPhase }).ToArray());
}
Check(ShiftPolicy.StillOwned(expected[0], new PixelOffset(0, 0)));
Check(!ShiftPolicy.StillOwned(expected[0], expected[1]));

string schema = File.ReadAllText(args[0])
    .Replace("@DEFAULT_INTERVAL@", ShiftPolicy.DefaultInterval.ToString())
    .Replace("@MIN_INTERVAL@", ShiftPolicy.MinInterval.ToString())
    .Replace("@MAX_INTERVAL@", ShiftPolicy.MaxInterval.ToString())
    .Replace("@DEFAULT_RADIUS@", ShiftPolicy.DefaultRadius.ToString())
    .Replace("@MIN_RADIUS@", ShiftPolicy.MinRadius.ToString())
    .Replace("@MAX_RADIUS@", ShiftPolicy.MaxRadius.ToString());
File.WriteAllText(args[1], schema);
File.WriteAllText(args[2], "// SPDX-License-Identifier: GPL-3.0-only\n" +
    $"export const cycles = {JsonSerializer.Serialize(cycles)};\n" +
    $"export const scheduleTransitions = {JsonSerializer.Serialize(ShiftSchedule.Transitions)};\n");
Console.WriteLine("PASS: shared trajectory, bounds, reversal and recovery ownership");

static void Check(bool condition)
{
    if (!condition)
        throw new InvalidOperationException("Shared pixel-shifting policy test failed");
}
