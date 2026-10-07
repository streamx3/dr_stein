// SPDX-License-Identifier: MIT
// The mockup's checkbox: a 16px rounded square, accent-filled with a surface
// inset when checked; an optional second line in muted text.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DrStein

CheckBox {
    id: control
    property string sublabel: ""

    spacing: 10
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    opacity: enabled ? 1 : Theme.disabledOpacity
    padding: 0

    indicator: Rectangle {
        x: control.leftPadding
        y: sub.visible ? 2 : (control.height - height) / 2
        width: 16; height: 16; radius: 4
        color: control.checked ? Theme.accent : "transparent"
        border.width: control.checked ? 0 : 1.5
        border.color: control.visualFocus ? Theme.accent : Theme.divider
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: 2
            color: Theme.surface
            visible: control.checked
        }
        Behavior on color { ColorAnimation { duration: Theme.quick } }
    }
    contentItem: Column {
        leftPadding: control.indicator.width + control.spacing
        spacing: 1
        Text {
            text: control.text
            font: control.font
            color: Theme.text
            wrapMode: Text.WordWrap
            width: parent.width > 0 ? parent.width - parent.leftPadding : implicitWidth
        }
        Text {
            id: sub
            visible: control.sublabel.length > 0
            text: control.sublabel
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            width: parent.width > 0 ? parent.width - parent.leftPadding : implicitWidth
        }
    }
}
