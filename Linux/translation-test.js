// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

import {WallpaperTranslation, WallpaperTranslations} from './extension/translation.js';

const actor = {
    position: [0, 0, 0],
    get_translation() { return this.position; },
    set_translation(...position) { this.position = position; },
    connect(_signal, callback) { this.destroy = callback; return 1; },
    disconnect() { this.destroy = null; },
};

function check(condition) {
    if (!condition)
        throw new Error('Wallpaper translation test failed');
}

let translation = new WallpaperTranslation(actor);
check(translation.shift(1, -1, 2));
check(actor.position.join() === '0.5,-0.5,0');
check(translation.shift(-1, 1, 2));
check(actor.position.join() === '-0.5,0.5,0');
translation.release();
check(actor.position.join() === '0,0,0');

translation = new WallpaperTranslation(actor);
translation.shift(1, 0, 1);
actor.set_translation(9, 0, 0);
check(!translation.shift(0, 1, 1));
translation.release();
check(actor.position.join() === '9,0,0');

translation = new WallpaperTranslation(actor);
actor.destroy();
translation.release();
check(!translation.shift(1, 0, 1));
print('PASS: scaling, bounded translation, restoration and external ownership');

const other = {...actor, position: [0, 0, 0]};
actor.position = [0, 0, 0];
const wallpapers = new WallpaperTranslations();
const targets = [{actor, scale: 1}, {actor: other, scale: 2}];
check(wallpapers.shift(targets, 1, -1));
check(actor.position.join() === '1,-1,0');
check(other.position.join() === '0.5,-0.5,0');
other.set_translation(9, 0, 0);
check(!wallpapers.shift(targets, -1, 1));
check(actor.position.join() === '0,0,0');
check(other.position.join() === '9,0,0');
check(wallpapers.isBlocked(other));
print('PASS: multi-monitor independent scaling and ownership-aware restoration');
