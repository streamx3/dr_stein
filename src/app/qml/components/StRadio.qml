// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic
import DrStein

RadioButton {
    id: control
    spacing: 8
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    padding: 0
    opacity: enabled ? 1 : Theme.disabledOpacity

    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        width: 14; height: 14; radius: 7
        color: "transparent"
        border.width: 1.5
        border.color: control.checked ? Theme.accent : (control.visualFocus ? Theme.accent : Theme.divider)
        Rectangle {
            anchors.centerIn: parent
            width: 6; height: 6; radius: 3
            color: Theme.accent
            visible: control.checked
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
    }
}
