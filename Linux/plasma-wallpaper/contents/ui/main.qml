// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import org.kde.taskmanager as TaskManager
import org.kde.plasma.plasmoid
import org.kde.plasma.workspace.dbus as DBus
import "policy.js" as Policy
import "shift-runtime.js" as Runtime

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
    readonly property var runtime: Runtime.createShiftRuntime(Policy.scheduleTransitions)
    property double lastPoll: 0
    property real offsetX: 0
    property real offsetY: 0

    function reset() {
        offsetX = 0
        offsetY = 0
        lastPoll = Date.now()
        runtime?.reset(lastPoll, seconds * 1000)
    }
    onActiveChanged: reset()
    onSecondsChanged: reset()
    onRadiusChanged: reset()
    onScaleFactorChanged: reset()
    onImageUrlChanged: reset()

    // 本插件只平移自己绘制的图层；底层保留原图，填补平移后露出的边缘。
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
        // 锁屏信号会增加代次；较早发出的查询不能用成功回复覆盖更新的锁屏事件。
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
            // 此处 Date.now() 使用系统时钟；时间倒退或轮询中断后重新计时，不补执行过期位移。
            if (now < root.lastPoll || now - root.lastPoll > 3000) {
                root.reset()
                return
            }
            root.lastPoll = now
            const step = root.runtime.poll(now, root.active ? "" : "paused", force, Policy.cycles[root.radius])
            if (step.restore) {
                root.offsetX = 0
                root.offsetY = 0
            }
            if (step.offset) {
                // 共享策略输出物理像素；每次写入相对原点的逻辑偏移，不能累加到现有平移。
                root.offsetX = step.offset[0] / root.scaleFactor
                root.offsetY = step.offset[1] / root.scaleFactor
            }
        }
    }
    Component.onCompleted: {
        lastShiftRequest = configuration.ShiftRequest
        reset()
    }
}
