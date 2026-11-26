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
        // 只有当前平移仍等于最后写入值时才拥有恢复权，不能覆盖其他组件之后的位移。
        return this.actor && this.actor.get_translation()
            .every((value, index) => value === this.last[index]);
    }

    shift(x, y, scale) {
        if (!this.owns())
            return false;

        // 恢复意图与 actor 同属 Shell 进程；进程退出会一起销毁，源壁纸文件和设置无需改写。
        // 物理像素除以各屏缩放得到逻辑坐标；fround 对齐 actor 的浮点存储，便于精确核对所有权。
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
        // 先检查所有屏幕的 actor，再开始写入；一屏失去所有权时停止并恢复仍属于本程序的其他屏。
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
            // 同一个 actor 被外部接管后不再尝试位移；换壁纸生成新 actor 后可以重新建立所有权。
            if (translation.actor && !translation.owns())
                this.#blocked.add(translation.actor);
            translation.release();
        }
        this.#entries.clear();
    }
}
