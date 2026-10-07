// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic
import DrStein

ToolButton {
    id: control
    property string icon_: "dot"
    property int iconSize: 15
    property color iconColor: hovered ? Theme.accentStep(300) : Theme.textSoft
    property string tip: ""

    implicitWidth: 28
    implicitHeight: 28
    hoverEnabled: true
    ToolTip.visible: tip.length > 0 && hovered
    ToolTip.text: tip
    ToolTip.delay: 600

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.pressed ? Theme.accentStep(900) : (control.hovered ? Theme.surface : "transparent")
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.icon_
            size: control.iconSize
            color: control.enabled ? control.iconColor : Theme.textDim
        }
    }
    opacity: enabled ? 1 : Theme.disabledOpacity
}
