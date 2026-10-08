// SPDX-License-Identifier: MIT
import QtQuick
import DrStein

Row {
    id: dot
    property string level: "ok"      // ok | info | warning | error
    property string text: ""
    spacing: 6
    Rectangle {
        width: 6; height: 6; radius: 3
        anchors.verticalCenter: parent.verticalCenter
        color: dot.text.length ? Theme.healthColor(dot.level) : "transparent"
    }
    Text {
        text: dot.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTiny
        color: dot.level === "warning" ? Theme.warning : dot.level === "error" ? Theme.danger : Theme.textSoft
        anchors.verticalCenter: parent.verticalCenter
    }
}
