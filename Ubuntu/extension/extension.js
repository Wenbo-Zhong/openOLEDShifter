// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as LoginManager from 'resource:///org/gnome/shell/misc/loginManager.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {cycles, scheduleTransitions} from './cycles.js';
import {WallpaperTranslations} from './translation.js';

const desktopInterface = `<node>
  <interface name="org.openoledshifter.Desktop">
    <method name="GetState"><arg type="s" direction="out"/></method>
    <method name="Apply">
      <arg type="i" direction="in"/><arg type="i" direction="in"/>
      <arg type="b" direction="in"/><arg type="b" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="ApplyOptions">
      <arg type="i" direction="in"/><arg type="i" direction="in"/>
      <arg type="b" direction="in"/><arg type="b" direction="in"/><arg type="b" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Pause"><arg type="s" direction="out"/></method>
    <method name="Shift"><arg type="s" direction="out"/></method>
    <method name="Toggle"><arg type="s" direction="out"/></method>
  </interface>
</node>`;

export default class OpenOLEDShifter extends Extension {
    enable() {
        this._signals = [];
        this._settings = this.getSettings();
        this._background = new Gio.Settings({schema_id: 'org.gnome.desktop.background'});
        this._interface = new Gio.Settings({schema_id: 'org.gnome.desktop.interface'});
        this._login = LoginManager.getLoginManager();
        this._sleeping = false;
        this._requested = this._settings.get_boolean('start-at-login');
        this._timer = 0;
        this._translations = new WallpaperTranslations();


        for (const [object, signal] of [
            [Main.layoutManager, 'monitors-changed'],
            [Main.sessionMode, 'updated'],
            [Main.overview, 'showing'],
            [Main.overview, 'hidden'],
            [global.display, 'in-fullscreen-changed'],
            [this._settings, 'changed::interval-seconds'],
            [this._settings, 'changed::radius'],
            [this._background, 'changed'],
            [this._interface, 'changed::color-scheme'],
        ]) {
            const id = object.connect(signal, () => this._restart());
            this._signals.push([object, id]);
        }
        this._restart();
        const sleepSignal = this._login.connect('prepare-for-sleep', (_manager, sleeping) => {
            this._sleeping = sleeping;
            if (this._requested)
                this._poll();
        });
        this._signals.push([this._login, sleepSignal]);
        this._desktopApi = Gio.DBusExportedObject.wrapJSObject(desktopInterface, this);
        this._desktopApi.export(Gio.DBus.session, '/org/openoledshifter/Desktop');
    }

    GetState() {
        return JSON.stringify({
            Settings: {
                IntervalSeconds: this._settings.get_int('interval-seconds'),
                Radius: this._settings.get_int('radius'),
                Enabled: this._requested,
                StartAtLogin: this._settings.get_boolean('start-at-login'),
                PauseFullscreen: this._settings.get_boolean('pause-fullscreen'),
            },
            Status: this._statusText ?? "",
        });
    }

    Apply(interval, radius, enabled, startAtLogin) {
        return this.ApplyOptions(interval, radius, enabled, startAtLogin,
            this._settings.get_boolean('pause-fullscreen'));
    }

    ApplyOptions(interval, radius, enabled, startAtLogin, pauseFullscreen) {
        const settings = this.getSettings();
        settings.delay();
        if (!settings.set_int('interval-seconds', interval) ||
            !settings.set_int('radius', radius) ||
            !settings.set_boolean('start-at-login', startAtLogin) ||
            !settings.set_boolean('pause-fullscreen', pauseFullscreen)) {
            settings.revert();
            throw new Error('Invalid or locked settings');
        }
        settings.apply();
        this._setRequested(enabled);
        return this.GetState();
    }

    Pause() {
        this._setRequested(false);
        return this.GetState();
    }

    Toggle() {
        this._setRequested(!this._requested);
        return this.GetState();
    }

    Shift() {
        if (this._requested)
            this._poll(true);
        return this.GetState();
    }

    _setRequested(active) {
        this._requested = active;
        this._restart();
    }

    _setStatus(text) {
        this._statusText = text;
    }

    _availability() {
        if (!global.context.get_wayland_compositor())
            return 'Unsupported: requires a GNOME Wayland session';
        const monitors = Main.layoutManager.monitors;
        if (monitors.length === 0)
            return 'Waiting for the desktop wallpaper';
        if (monitors.some(({geometry_scale: scale}) =>
            scale < 1 || !(Number.isInteger(scale) || scale === 1.25 || scale === 1.5)))
            return 'Unsupported: use integer, 125%, or 150% display scaling';
        if (Main.sessionMode.isLocked || this._sleeping ||
            Main.overview.visible || (this._settings.get_boolean('pause-fullscreen') && monitors.some(monitor => monitor.inFullscreen)))
            return 'Paused: session, overview or full-screen activity';
        const style = this._background.get_string('picture-options');
        const dark = this._interface.get_string('color-scheme') === 'prefer-dark';
        const uri = this._background.get_string(dark ? 'picture-uri-dark' : 'picture-uri');
        if (style === 'none' || !/^file:\/\/.*\.(png|jpe?g|webp|avif|bmp|svg|heic|tiff?)$/i.test(uri))
            return 'Unsupported: requires a local static wallpaper';

        const targets = monitors.map(monitor => ({
            actor: Main.layoutManager._bgManagers?.[monitor.index]?.backgroundActor,
            scale: monitor.geometry_scale,
        }));
        if (targets.some(({actor}) => !actor?.mapped))
            return 'Waiting for the desktop wallpaper';
        if (targets.some(({actor}) => this._translations.isBlocked(actor)))
            return 'Paused: another component moved the wallpaper';
        return targets;
    }

    _poll(force = false) {
        const target = this._availability();
        const reason = typeof target === 'string' ? target : '';
        const now = GLib.get_monotonic_time();
        // The bit order and transition table come from the shared core.
        const flags = (reason ? 1 : 0)
            | (reason !== this._pauseReason ? 2 : 0)
            | (this._pauseReason ? 4 : 0)
            | (now >= this._due ? 8 : 0)
            | (force ? 16 : 0);
        const decision = scheduleTransitions[flags];
        this._pauseReason = reason;
        if (decision.ResetDeadline)
            this._due = now + this._interval;
        if (decision.Restore) {
            this._translations.release();
            this._phase = 0;
        }
        if (reason) {
            this._setStatus(reason);
            return;
        }
        if (decision.Shift)
            this._shift(target);
        else if (this._translations.empty)
            this._setStatus('Waiting for the next wallpaper shift');
    }

    _shift(target) {
        if (!this._translations.matches(target))
            this._phase = 0;
        const cycle = cycles[this._settings.get_int('radius')];
        const next = cycle[this._phase][2];
        const [x, y] = cycle[next];
        if (!this._translations.shift(target, x, y)) {
            this._setStatus('Paused: another component moved the wallpaper');
            return;
        }

        this._setStatus(`Wallpaper offset: ${x}, ${y} physical px`);
        this._phase = next;
    }

    _stop() {
        if (this._timer) {
            GLib.source_remove(this._timer);
            this._timer = 0;
        }
        this._translations.release();
    }

    _restart() {
        this._stop();
        if (!this._requested) {
            this._setStatus('Wallpaper shifting is off');
            return;
        }

        this._phase = 0;
        this._interval = this._settings.get_int('interval-seconds') * 1_000_000;
        this._pauseReason = '';
        this._due = GLib.get_monotonic_time() + this._interval;
        this._poll();
        this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1, () => {
            this._poll();
            return GLib.SOURCE_CONTINUE;
        });
    }

    disable() {
        this._desktopApi?.unexport();
        this._desktopApi = null;
        this._stop();
        for (const [object, id] of this._signals)
            object.disconnect(id);
        this._signals = [];
        this._settings = this._background = this._interface = this._login = null;
        this._translations = null;
    }
}
