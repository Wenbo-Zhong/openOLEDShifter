// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as LoginManager from 'resource:///org/gnome/shell/misc/loginManager.js';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import {cycles} from './cycles.js';
import {WallpaperTranslation} from './translation.js';

const desktopInterface = `<node>
  <interface name="org.openoledshifter.Desktop">
    <method name="GetState"><arg type="s" direction="out"/></method>
    <method name="Apply">
      <arg type="i" direction="in"/><arg type="i" direction="in"/>
      <arg type="b" direction="in"/><arg type="b" direction="in"/>
      <arg type="s" direction="out"/>
    </method>
    <method name="Pause"><arg type="s" direction="out"/></method>
    <method name="Shift"><arg type="s" direction="out"/></method>
  </interface>
</node>`;

export default class OpenOLEDShifter extends Extension {
    enable() {
        this._signals = [];
        this._settings = this.getSettings();
        this._background = new Gio.Settings({schema_id: 'org.gnome.desktop.background'});
        this._interface = new Gio.Settings({schema_id: 'org.gnome.desktop.interface'});
        this._login = LoginManager.getLoginManager();
        this._requested = this._settings.get_boolean('start-at-login');
        this._timer = 0;
        this._translation = null;
        this._blockedActors = new WeakSet();

        this._indicator = new PanelMenu.Button(0.0, 'OpenOLEDShifter');
        this._indicator.add_child(new St.Icon({
            icon_name: 'video-display-symbolic',
            style_class: 'system-status-icon',
        }));
        this._status = new PopupMenu.PopupMenuItem('', {reactive: false});
        this._toggle = new PopupMenu.PopupSwitchMenuItem('Shift desktop wallpaper', this._requested);
        this._toggle.connect('toggled', (_item, active) => {
            this._requested = active;
            this._restart();
        });
        this._indicator.menu.addMenuItem(this._status);
        this._indicator.menu.addMenuItem(this._toggle);
        this._indicator.menu.addAction('Settings', () => {
            const app = Shell.AppSystem.get_default().lookup_app('org.openoledshifter.Desktop.desktop');
            if (app)
                app.activate();
            else
                Main.notify('OpenOLEDShifter', 'Install the desktop settings application to open Settings.');
        });
        Main.panel.addToStatusArea(this.uuid, this._indicator);

        for (const [object, signal] of [
            [Main.layoutManager, 'monitors-changed'],
            [Main.sessionMode, 'updated'],
            [Main.overview, 'showing'],
            [Main.overview, 'hidden'],
            [global.display, 'in-fullscreen-changed'],
            [this._login, 'prepare-for-sleep'],
            [this._settings, 'changed::interval-seconds'],
            [this._settings, 'changed::radius'],
            [this._background, 'changed'],
            [this._interface, 'changed::color-scheme'],
        ]) {
            const id = object.connect(signal, () => this._restart());
            this._signals.push([object, id]);
        }
        this._restart();
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
            },
            Status: this._status.label.text,
        });
    }

    Apply(interval, radius, enabled, startAtLogin) {
        const settings = this.getSettings();
        settings.delay();
        if (!settings.set_int('interval-seconds', interval) ||
            !settings.set_int('radius', radius) ||
            !settings.set_boolean('start-at-login', startAtLogin)) {
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

    Shift() {
        if (this._requested && this._step === 0 && !this._translation)
            this._tick();
        if (this._requested)
            this._tick();
        return this.GetState();
    }

    _setRequested(active) {
        this._requested = active;
        this._toggle.setToggleState(active);
        this._restart();
    }

    _availability() {
        if (!global.context.get_wayland_compositor())
            return 'Unsupported: requires a GNOME Wayland session';
        const monitors = Main.layoutManager.monitors;
        if (monitors.length !== 1)
            return 'Unsupported: requires one active display';
        const monitor = monitors[0];
        if (!Number.isInteger(monitor.geometry_scale) || monitor.geometry_scale < 1)
            return 'Unsupported: requires integer display scaling';
        if (Main.sessionMode.isLocked || this._login.preparingForSleep ||
            Main.overview.visible || monitor.inFullscreen)
            return 'Paused: session, overview or full-screen activity';
        const style = this._background.get_string('picture-options');
        const dark = this._interface.get_string('color-scheme') === 'prefer-dark';
        const uri = this._background.get_string(dark ? 'picture-uri-dark' : 'picture-uri');
        if (style === 'none' || !/^file:\/\/.*\.(png|jpe?g|webp|avif|bmp|svg|heic|tiff?)$/i.test(uri))
            return 'Unsupported: requires a local static wallpaper';

        const actor = Main.layoutManager._bgManagers?.[monitor.index]?.backgroundActor;
        if (!actor || !actor.mapped)
            return 'Waiting for the desktop wallpaper';
        if (this._blockedActors.has(actor))
            return 'Paused: another component moved the wallpaper';
        return {actor, scale: monitor.geometry_scale};
    }

    _release() {
        if (this._translation?.actor && !this._translation.owns())
            this._blockedActors.add(this._translation.actor);
        this._translation?.release();
        this._translation = null;
    }

    _tick() {
        const target = this._availability();
        if (typeof target === 'string') {
            this._release();
            this._step = 0;
            this._status.label.text = target;
            return;
        }

        const {actor, scale} = target;
        if (this._translation?.actor !== actor) {
            this._release();
            this._translation = new WallpaperTranslation(actor);
            this._step = 0;
        }
        const cycle = cycles[this._settings.get_int('radius')];
        const [x, y] = cycle[this._step];
        if (!this._translation.shift(x, y, scale)) {
            this._release();
            this._status.label.text = 'Paused: another component moved the wallpaper';
            return;
        }

        this._status.label.text = `Wallpaper offset: ${x}, ${y} physical px`;
        this._step = (this._step + 1) % cycle.length;
    }

    _stop() {
        if (this._timer) {
            GLib.source_remove(this._timer);
            this._timer = 0;
        }
        this._release();
    }

    _restart() {
        this._stop();
        if (!this._requested) {
            this._status.label.text = 'Wallpaper shifting is off';
            return;
        }

        this._step = 0;
        this._tick();
        this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT,
            this._settings.get_int('interval-seconds'), () => {
                this._tick();
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
        this._indicator.destroy();
        this._indicator = this._toggle = this._status = null;
        this._settings = this._background = this._interface = this._login = null;
        this._blockedActors = null;
    }
}
