// SPDX-License-Identifier: MIT
// Uppercase column captions over a table, with the fading rule beneath.
import QtQuick
import QtQuick.Layouts
import DrStein

Item {
    id: header
    property var columns: []        // [{label, width (0 = fill), align}]
    property int leftPad: 10
    property int rightPad: 10
    property int spacing: 12
    implicitHeight: 20
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: header.leftPad
        anchors.rightMargin: header.rightPad
        anchors.bottomMargin: 6
        spacing: header.spacing
        Repeater {
            model: header.columns
            delegate: Kicker {
                required property var modelData
                text: modelData.label
                Layout.fillWidth: !modelData.width
                Layout.preferredWidth: modelData.width ? modelData.width : -1
                Layout.minimumWidth: modelData.width ? modelData.width : 0
                Layout.maximumWidth: modelData.width ? modelData.width : -1
                horizontalAlignment: modelData.align === "right" ? Text.AlignRight : Text.AlignLeft
            }
        }
    }
    FadeDivider { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
}
