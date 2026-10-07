// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts
import DrStein

Item {
    property string title: ""
    property string message: ""
    property string icon_: "info"
    property alias actions: actionRow.data
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 460)
        spacing: Theme.space3
        Icon { name: icon_; size: 28; color: Theme.textDim; Layout.alignment: Qt.AlignHCenter }
        Text {
            Layout.fillWidth: true
            text: title
            visible: title.length > 0
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontH3
            font.weight: Theme.headingWeight
            color: Theme.textBright
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: message
            visible: message.length > 0
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        Row { id: actionRow; Layout.alignment: Qt.AlignHCenter; spacing: 6 }
    }
}
