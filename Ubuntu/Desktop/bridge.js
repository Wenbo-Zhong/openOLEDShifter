// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import System from 'system';

try {
    const [method, interval, radius, enabled, startup] = ARGV;
    const methods = ['GetState', 'Apply', 'Pause', 'Shift'];
    if (!methods.includes(method))
        throw new Error('Unknown OpenOLEDShifter command');
    const parameters = method === 'Apply'
        ? new GLib.Variant('(iibb)', [Number(interval), Number(radius), enabled === 'true', startup === 'true'])
        : null;
    const result = Gio.DBus.session.call_sync(
        'org.gnome.Shell', '/org/openoledshifter/Desktop', 'org.openoledshifter.Desktop',
        method, parameters, new GLib.VariantType('(s)'),
        Gio.DBusCallFlags.NO_AUTO_START, 2000, null);
    print(result.deepUnpack()[0]);
} catch (error) {
    printerr(error.message);
    System.exit(1);
}
