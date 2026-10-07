// SPDX-License-Identifier: MIT
import QtQuick
import DrStein

Rectangle {
    id: tag
    property string text: ""
    property string variant: "neutral"   // neutral | accent | outline | ok | warning | danger
    property bool dot: false
    property color dotColor: Theme.accent

    readonly property color fill: variant === "accent" ? Theme.accentStep(800)
                                : variant === "neutral" ? Theme.neutralStep(800)
                                : variant === "ok" ? Qt.rgba(Theme.ok.r, Theme.ok.g, Theme.ok.b, 0.18)
                                : variant === "warning" ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.18)
                                : variant === "danger" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.18)
                                : "transparent"
    readonly property color fg: variant === "accent" ? Theme.accentStep(100)
                              : variant === "neutral" ? Theme.neutralStep(100)
                              : variant === "ok" ? Theme.ok
                              : variant === "warning" ? Theme.warning
                              : variant === "danger" ? Theme.danger
                              : Theme.textSoft

    implicitHeight: 20
    implicitWidth: row.implicitWidth + 16
    radius: 10
    color: fill
    border.width: variant === "outline" ? 1 : 0
    border.color: Theme.edge2

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Rectangle {
            visible: tag.dot
            width: 6; height: 6; radius: 3
            color: tag.dotColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: tag.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            font.weight: Font.Medium
            color: tag.fg
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
