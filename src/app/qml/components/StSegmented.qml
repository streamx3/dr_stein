// SPDX-License-Identifier: MIT
// The ".seg" control: joined options, the current one ringed in the accent.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import DrStein

Rectangle {
    id: seg
    property var model: []          // [{label, value}] or ["A", "B"]
    property var currentValue: null
    property bool stretch: false
    property bool small: true
    signal picked(var value)

    radius: Theme.radiusMd
    color: Theme.surface
    border.width: 1
    border.color: Theme.edge1
    implicitHeight: row.implicitHeight + 2
    implicitWidth: row.implicitWidth + 2
    clip: true

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        Repeater {
            model: seg.model
            delegate: Rectangle {
                id: opt
                required property var modelData
                required property int index
                readonly property string label: typeof modelData === "object" ? modelData.label : modelData
                readonly property var value: typeof modelData === "object" ? modelData.value : modelData
                readonly property bool current: seg.currentValue === value
                Layout.fillWidth: seg.stretch
                Layout.fillHeight: true
                implicitWidth: txt.implicitWidth + 24
                implicitHeight: txt.implicitHeight + (seg.small ? 12 : 14)
                color: current ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08) : (ma.containsMouse ? Theme.sunken : "transparent")
                radius: Theme.radiusMd - 1
                border.width: current ? 1 : 0
                border.color: Theme.accent
                Rectangle {   // separator
                    visible: opt.index > 0 && !opt.current
                    width: 1; height: parent.height - 10
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    color: Theme.divider
                }
                Text {
                    id: txt
                    anchors.centerIn: parent
                    text: opt.label
                    font.family: Theme.fontFamily
                    font.pixelSize: seg.small ? Theme.fontSmall : Theme.fontSize
                    font.weight: Font.Medium
                    color: opt.current ? Theme.accent : Theme.textBright
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.picked(opt.value)
                }
            }
        }
    }
}
