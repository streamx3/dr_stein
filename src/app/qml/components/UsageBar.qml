// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts
import DrStein

ColumnLayout {
    property string label: "used"
    property real fraction: 0
    property string valueText: ""
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Text { text: label; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textSoft; Layout.fillWidth: true; elide: Text.ElideRight }
        Text { text: valueText.length ? valueText : Math.round(fraction * 100) + "%"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textSoft }
    }
    Rectangle {
        Layout.fillWidth: true
        height: 5
        radius: 3
        color: Theme.sunken
        Rectangle {
            width: Math.max(2, parent.width * Math.min(1, Math.max(0, fraction)))
            height: parent.height
            radius: 3
            color: Theme.accent
            Rectangle { anchors.fill: parent; anchors.margins: -3; radius: 5; color: Theme.accentStep(700); opacity: 0.25; z: -1 }
        }
    }
}
