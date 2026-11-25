// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

namespace oled::win {
void initialize(bool smoke);
void tick();
void set_locked(bool locked);
void set_suspended(bool suspended);
void set_ending(bool ending);
void displays_changed();
bool shutdown();
}
