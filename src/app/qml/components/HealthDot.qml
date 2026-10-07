// SPDX-License-Identifier: MIT
import QtQuick
import DrStein

Row {
    property string level: "ok"      // ok | info | warning | error
    property string text: ""
    spacing: 6
    Rectangle {
        width: 6; height: 6; radius: 3
        anchors.verticalCenter: parent.verticalCenter
        color: text.length ? Theme.healthColor(level) : "transparent"
    }
    Text {
        text: parent.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTiny
        color: level === "warning" ? Theme.warning : level === "error" ? Theme.danger : Theme.textSoft
        anchors.verticalCenter: parent.verticalCenter
    }
}
