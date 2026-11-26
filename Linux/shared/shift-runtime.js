// SPDX-License-Identifier: GPL-3.0-only
// transitions 和 cycle 来自 C++ 策略生成器；这里仅执行表，不另外实现一套调度规则。
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
            // 位顺序与 NativeApp/tools/generate_policy.cpp 一致；pause 是暂停原因，不是单纯布尔值。
            const flags = (pause ? 1 : 0) | (pause !== reason ? 2 : 0)
                | (reason ? 4 : 0) | (now >= deadline ? 8 : 0) | (force ? 16 : 0);
            const decision = transitions[flags];
            reason = pause;
            if (decision.ResetDeadline)
                deadline = now + interval;
            // 暂停还原或桌面目标变更后回到零相位，下次位移从新周期的第一步开始。
            if (decision.Restore || resetPhase)
                phase = 0;
            if (decision.Shift)
                phase = cycle[phase][2];
            return {restore: decision.Restore, offset: decision.Shift ? cycle[phase] : null};
        },
    };
}
