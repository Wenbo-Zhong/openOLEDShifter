// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

export class WallpaperTranslation {
    constructor(actor) {
        this.actor = actor;
        this.last = [0, 0, 0];
        this.destroyId = actor.connect('destroy', () => {
            this.actor = null;
        });
    }

    owns() {
        return this.actor && this.actor.get_translation()
            .every((value, index) => value === this.last[index]);
    }

    shift(x, y, scale) {
        if (!this.owns())
            return false;

        // Writable recovery intent belongs to the same Shell process as the
        // mutation. A process exit destroys the actor and its translation.
        this.last = [Math.fround(x / scale), Math.fround(y / scale), 0];
        this.actor.set_translation(...this.last);
        return true;
    }

    release() {
        if (!this.actor)
            return;

        if (this.owns())
            this.actor.set_translation(0, 0, 0);
        this.actor.disconnect(this.destroyId);
        this.actor = null;
    }
}
