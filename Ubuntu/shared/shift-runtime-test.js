// SPDX-License-Identifier: GPL-3.0-only
import {createShiftRuntime} from './shift-runtime.js';
import {cycles, scheduleTransitions} from './cycles.js';

function check(condition) {
    if (!condition)
        throw Error('Shared shift runtime regression');
}

const state = createShiftRuntime(scheduleTransitions);
const cycle = cycles[1];
state.reset(0, 200);
check(state.poll(199, '', false, cycle).offset === null);
check(state.poll(200, '', false, cycle).offset === cycle[1]);
const locked = state.poll(201, 'locked', true, cycle);
check(locked.restore && locked.offset === null);
check(!state.poll(202, 'locked', false, cycle).restore);
check(state.poll(900, '', false, cycle).offset === null);
check(state.poll(1099, '', false, cycle).offset === null);
check(state.poll(1100, '', false, cycle).offset === cycle[1]);
check(state.poll(1101, '', true, cycle).offset === cycle[2]);
check(state.poll(1102, '', true, cycle, true).offset === cycle[1]);
print('PASS: shared runtime timing, forced shifts, pause, resume and target reset');
