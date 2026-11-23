// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
namespace OpenOLEDShifter;

public readonly record struct ScheduleDecision(bool Restore, bool Shift);
public readonly record struct ScheduleTransition(bool Restore, bool Shift, bool ResetDeadline);

public sealed class ShiftSchedule
{
    private string pauseReason = "";
    private int interval;
    public DateTimeOffset Due { get; private set; }
    public ShiftSchedule(int intervalSeconds, DateTimeOffset now) => Reset(intervalSeconds, now);

    // Bit order: blocked, reason changed, previously blocked, deadline reached, forced.
    // Exported to GNOME at build time so both runtimes use the same transition rules.
    public static IReadOnlyList<ScheduleTransition> Transitions { get; } =
        Array.AsReadOnly(Enumerable.Range(0, 32).Select(flags =>
        {
            bool blocked = (flags & 1) != 0;
            bool changed = (flags & 2) != 0;
            bool wasBlocked = (flags & 4) != 0;
            bool due = (flags & 8) != 0;
            bool forced = (flags & 16) != 0;
            bool restore = blocked && changed;
            bool shift = !blocked && (forced || (due && !wasBlocked));
            return new ScheduleTransition(restore, shift, restore || shift || (!blocked && wasBlocked));
        }).ToArray());

    public void Reset(int intervalSeconds, DateTimeOffset now)
    {
        if (intervalSeconds is < ShiftPolicy.MinInterval or > ShiftPolicy.MaxInterval)
            throw new ArgumentOutOfRangeException(nameof(intervalSeconds));
        interval = intervalSeconds;
        pauseReason = "";
        Due = now.AddSeconds(interval);
    }

    public ScheduleDecision Poll(DateTimeOffset now, string blockedReason, bool force = false)
    {
        int flags = (blockedReason.Length > 0 ? 1 : 0)
            | (pauseReason != blockedReason ? 2 : 0)
            | (pauseReason.Length > 0 ? 4 : 0)
            | (now >= Due ? 8 : 0)
            | (force ? 16 : 0);
        var transition = Transitions[flags];
        pauseReason = blockedReason;
        if (transition.ResetDeadline)
            Due = now.AddSeconds(interval);
        return new(transition.Restore, transition.Shift);
    }
}
