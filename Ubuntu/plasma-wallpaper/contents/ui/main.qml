// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import org.kde.taskmanager as TaskManager
import org.kde.plasma.plasmoid
import org.kde.plasma.workspace.dbus as DBus
import "policy.js" as Policy

WallpaperItem {
    id: root
    clip: true
    readonly property string imageUrl: configuration.Image || ""
    readonly property bool localImage: imageUrl.startsWith("file:///")
    readonly property int seconds: Math.max(Policy.defaults.minInterval,
        Math.min(Policy.defaults.maxInterval, configuration.Interval || Policy.defaults.interval))
    readonly property int radius: Math.max(Policy.defaults.minRadius,
        Math.min(Policy.defaults.maxRadius, configuration.Radius || Policy.defaults.radius))
    readonly property real scaleFactor: Screen.devicePixelRatio
    property bool fullscreen: false
    property int lastShiftRequest: 0
    property bool locked: true
    property bool sleeping: false
    property bool checkingLock: false
    property int lockGeneration: 0
    readonly property bool active: configuration.Enabled && localImage &&
        shifted.status === Image.Ready && visible && !locked && !sleeping && !fullscreen
    property int phase: 0
    property double deadline: 0
    property double lastPoll: 0
    property bool wasBlocked: true
    property real offsetX: 0
    property real offsetY: 0

    function reset() {
        phase = 0
        offsetX = 0
        offsetY = 0
        lastPoll = Date.now()
        deadline = lastPoll + seconds * 1000
        wasBlocked = !active
    }
    onActiveChanged: reset()
    onSecondsChanged: reset()
    onRadiusChanged: reset()
    onScaleFactorChanged: reset()
    onImageUrlChanged: reset()

    // Keep the source image under the translated layer to fill its exposed edges.
    Image {
        anchors.fill: parent
        source: root.localImage ? root.imageUrl : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    Image {
        id: shifted
        anchors.fill: parent
        source: root.localImage ? root.imageUrl : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: root.offsetX !== 0 || root.offsetY !== 0
        transform: Translate { x: root.offsetX; y: root.offsetY }
    }

    function checkLock() {
        if (checkingLock)
            return
        checkingLock = true
        const generation = lockGeneration
        DBus.SessionBus.asyncCall({
            service: "org.freedesktop.ScreenSaver", path: "/ScreenSaver",
            iface: "org.freedesktop.ScreenSaver", member: "GetActive"
        }, reply => {
            checkingLock = false
            if (generation === lockGeneration)
                locked = reply.value !== false
        }, () => {
            checkingLock = false
            if (generation === lockGeneration)
                locked = true
        })
    }
    DBus.SignalWatcher {
        busType: DBus.BusType.Session
        service: "org.freedesktop.ScreenSaver"
        path: "/ScreenSaver"
        iface: "org.freedesktop.ScreenSaver"
        function dbusActiveChanged(active) {
            ++root.lockGeneration
            root.locked = active !== false
        }
    }
    DBus.SignalWatcher {
        busType: DBus.BusType.System
        service: "org.freedesktop.login1"
        path: "/org/freedesktop/login1"
        iface: "org.freedesktop.login1.Manager"
        function dbusPrepareForSleep(entering) {
            root.sleeping = entering !== false
            if (!root.sleeping)
                root.checkLock()
        }
    }
    TaskManager.TasksModel {
        id: tasks
        groupMode: TaskManager.TasksModel.GroupDisabled
    }
    Timer {
        interval: 1000
        running: root.configuration.Enabled
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            root.checkLock()
            root.fullscreen = root.configuration.PauseFullscreen &&
                tasks.data(tasks.activeTask, TaskManager.TasksModel.IsFullScreen) === true
            const request = root.configuration.ShiftRequest
            const force = request !== root.lastShiftRequest
            root.lastShiftRequest = request
            const now = Date.now()
            if (now < root.lastPoll || now - root.lastPoll > 3000) {
                root.reset()
                return
            }
            root.lastPoll = now
            const blocked = !root.active
            const flags = (blocked ? 1 : 0) | (blocked !== root.wasBlocked ? 2 : 0)
                | (root.wasBlocked ? 4 : 0) | (now >= root.deadline ? 8 : 0) | (force ? 16 : 0)
            const step = Policy.scheduleTransitions[flags]
            root.wasBlocked = blocked
            if (step.Restore)
                root.reset()
            if (step.ResetDeadline)
                root.deadline = now + root.seconds * 1000
            if (step.Shift) {
                const cycle = Policy.cycles[root.radius]
                root.phase = cycle[root.phase][2]
                const frame = cycle[root.phase]
                root.offsetX = frame[0] / root.scaleFactor
                root.offsetY = frame[1] / root.scaleFactor
            }
        }
    }
    Component.onCompleted: {
        lastShiftRequest = configuration.ShiftRequest
        reset()
    }
}
