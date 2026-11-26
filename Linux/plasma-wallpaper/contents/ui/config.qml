// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "policy.js" as Policy

Kirigami.FormLayout {
    id: form
    property var configDialog
    property var wallpaperConfiguration
    property string cfg_Image
    property alias cfg_Enabled: enabledToggle.checked
    property int cfg_Interval
    property int cfg_Radius

    function useCurrentImage() {
        // Plasma passes the existing wallpaper object to this settings view,
        // including the containment for the screen being configured.
        if (cfg_Image || typeof wallpaper === "undefined" ||
            !wallpaper || wallpaper.pluginName !== "org.kde.image")
            return

        // The stock Image wallpaper exposes its resolved URL on the image view.
        // This also handles the theme default and directories packaged as wallpapers.
        for (const item of wallpaper.children || []) {
            const resolved = item.modelImage?.toString() || ""
            if (resolved.startsWith("file:///")) {
                cfg_Image = resolved
                return
            }
        }
        const configured = wallpaper.configuration?.Image?.toString() || ""
        if (/^file:\/\/\/.*\.(png|jpe?g|webp|avif|bmp|tiff?|svg)$/i.test(configured))
            cfg_Image = configured
    }

    Component.onCompleted: Qt.callLater(useCurrentImage)

    RowLayout {
        Kirigami.FormData.label: i18n("Wallpaper image:")
        Label {
            text: form.cfg_Image || i18n("No image selected")
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
        Button {
            text: i18n("Choose Image…")
            onClicked: chooser.open()
        }
    }
    FileDialog {
        id: chooser
        nameFilters: [i18n("Images (*.png *.jpg *.jpeg *.webp *.avif *.bmp)")]
        onAccepted: form.cfg_Image = selectedFile.toString()
    }
    CheckBox {
        id: enabledToggle
        text: i18n("Enable wallpaper pixel shifting")
    }
    SpinBox {
        Kirigami.FormData.label: i18n("Interval (seconds):")
        from: Policy.defaults.minInterval
        to: Policy.defaults.maxInterval
        value: form.cfg_Interval || Policy.defaults.interval
        editable: true
        onValueModified: form.cfg_Interval = value
    }
    SpinBox {
        Kirigami.FormData.label: i18n("Maximum shift (physical pixels):")
        from: Policy.defaults.minRadius
        to: Policy.defaults.maxRadius
        value: form.cfg_Radius || Policy.defaults.radius
        onValueModified: form.cfg_Radius = value
    }
}
