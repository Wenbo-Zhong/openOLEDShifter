// SPDX-License-Identifier: GPL-3.0-only
export function createShiftRuntime(transitions) {
    let interval = 0;
    let deadline = 0;
    let reason = '';
    let phase = 0;
    return {
        reset(now, milliseconds) {
            interval = milliseconds;
            deadline = now + interval;
            reason = '';
            phase = 0;
        },
        poll(now, pause, force, cycle, resetPhase = false) {
            const flags = (pause ? 1 : 0) | (pause !== reason ? 2 : 0)
                | (reason ? 4 : 0) | (now >= deadline ? 8 : 0) | (force ? 16 : 0);
            const decision = transitions[flags];
            reason = pause;
            if (decision.ResetDeadline)
                deadline = now + interval;
            if (decision.Restore || resetPhase)
                phase = 0;
            if (decision.Shift)
                phase = cycle[phase][2];
            return {restore: decision.Restore, offset: decision.Shift ? cycle[phase] : null};
        },
    };
}
