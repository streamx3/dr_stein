// SPDX-License-Identifier: MIT
// Nocturne buttons: outlined, never filled. primary = accent outline,
// secondary = neutral outline, ghost = no outline, danger = danger outline.
import QtQuick
import QtQuick.Controls.Basic
import DrStein

Button {
    id: control
    property string variant: "secondary"
    property bool small: false
    property string icon_: ""
    property int iconSize: small ? 13 : 14
    property string subtext: ""
    property bool dashed: false

    readonly property color lineColor: variant === "primary" ? Theme.accent
                                     : variant === "danger" ? Theme.danger
                                     : variant === "ghost" ? "transparent" : Theme.edge2
    readonly property color fgColor: variant === "primary" ? Theme.accentStep(300)
                                   : variant === "danger" ? Theme.danger
                                   : Theme.textBright

    hoverEnabled: true
    padding: small ? 5 : 7
    leftPadding: small ? 10 : 13
    rightPadding: small ? 10 : 13
    font.family: Theme.fontFamily
    font.pixelSize: small ? Theme.fontSmall : Theme.fontSize
    font.weight: Font.Medium
    opacity: enabled ? 1 : Theme.disabledOpacity

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.pressed ? Qt.rgba(control.lineColor.r, control.lineColor.g, control.lineColor.b, 0.18)
             : control.hovered ? Qt.rgba(control.lineColor.r, control.lineColor.g, control.lineColor.b, control.variant === "ghost" ? 0.0 : 0.08)
             : "transparent"
        border.width: control.visualFocus ? 2 : (control.variant === "ghost" ? 0 : 1)
        border.color: control.visualFocus ? Theme.accent : control.lineColor
        Behavior on color { ColorAnimation { duration: Theme.quick } }
        // Dashed outline for "add" affordances.
        Canvas {
            anchors.fill: parent
            visible: control.dashed
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = control.lineColor
                ctx.lineWidth = 1
                ctx.setLineDash([4, 4])
                ctx.beginPath()
                ctx.roundedRect(0.5, 0.5, width - 1, height - 1, Theme.radiusMd, Theme.radiusMd)
                ctx.stroke()
            }
        }
    }
    contentItem: Column {
        spacing: 2
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 7
            Icon {
                visible: control.icon_.length > 0
                name: control.icon_
                size: control.iconSize
                color: control.hovered ? Theme.accentStep(300) : control.fgColor
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: control.text
                font: control.font
                color: control.hovered && control.variant !== "danger" ? Theme.accentStep(300) : control.fgColor
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text {
            visible: control.subtext.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: control.subtext
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            color: Theme.textMuted
        }
    }
}
