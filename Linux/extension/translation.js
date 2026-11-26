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

export class WallpaperTranslations {
    #entries = new Map();
    #blocked = new WeakSet();

    get empty() {
        return this.#entries.size === 0;
    }

    isBlocked(actor) {
        return this.#blocked.has(actor);
    }

    matches(targets) {
        return this.#entries.size === targets.length &&
            targets.every(({actor, scale}) => this.#entries.get(actor)?.scale === scale);
    }

    shift(targets, x, y) {
        if (!this.matches(targets)) {
            this.release();
            for (const {actor, scale} of targets)
                this.#entries.set(actor, {scale, translation: new WallpaperTranslation(actor)});
        }
        // Check all actors before changing any output.
        if (targets.some(({actor}) => this.isBlocked(actor) || !this.#entries.get(actor).translation.owns())) {
            this.release();
            return false;
        }

        for (const {scale, translation} of this.#entries.values())
            translation.shift(x, y, scale);
        return true;
    }

    release() {
        for (const {translation} of this.#entries.values()) {
            if (translation.actor && !translation.owns())
                this.#blocked.add(translation.actor);
            translation.release();
        }
        this.#entries.clear();
    }
}
