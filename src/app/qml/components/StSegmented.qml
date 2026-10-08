// SPDX-License-Identifier: MIT
// The ".seg" control: linked options, rounded at the corners of the block and square
// where they join (per-corner radii, Qt 6.7), the current one ringed in the accent with
// the same corners. With wrap, the options flow into as many rows as the width allows.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import DrStein

Rectangle {
    id: seg
    property var model: []          // [{label, value}] or ["A", "B"]
    property var currentValue: null
    property bool stretch: false    // single row: options share the width
    property bool wrap: false       // several rows when narrower than one row needs
    property bool small: true
    signal picked(var value)

    readonly property int r: Theme.radiusMd - 1
    // One row's natural width: the most the control asks for; a layout may give it less when wrap is on.
    readonly property real oneRowWidth: {
        var w = 2
        for (var i = 0; i < flow.count; ++i) w += flow.itemAt(i) ? flow.itemAt(i).implicitWidth : 0
        return w
    }
    readonly property real widestOption: {
        var w = 0
        for (var i = 0; i < flow.count; ++i) if (flow.itemAt(i)) w = Math.max(w, flow.itemAt(i).implicitWidth)
        return w + 2
    }

    radius: Theme.radiusMd
    color: Theme.surface
    border.width: 1
    border.color: Theme.edge1
    implicitHeight: (wrap ? flowBox.implicitHeight : row.implicitHeight) + 2
    implicitWidth: wrap ? oneRowWidth : row.implicitWidth + 2
    clip: true

    component Option: Rectangle {
        id: opt
        required property var modelData
        required property int index
        // Which corners of the whole block this option touches; those are rounded.
        property bool tl: false
        property bool tr: false
        property bool bl: false
        property bool br: false
        readonly property string label: typeof modelData === "object" ? modelData.label : modelData
        readonly property var value: typeof modelData === "object" ? modelData.value : modelData
        readonly property bool current: seg.currentValue === value
        implicitWidth: txt.implicitWidth + 24
        implicitHeight: txt.implicitHeight + (seg.small ? 12 : 14)
        color: current ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08) : (ma.containsMouse ? Theme.sunken : "transparent")
        topLeftRadius: tl ? seg.r : 0
        topRightRadius: tr ? seg.r : 0
        bottomLeftRadius: bl ? seg.r : 0
        bottomRightRadius: br ? seg.r : 0
        border.width: current ? 1 : 0
        border.color: Theme.accent
        Rectangle {   // separator to the option on the left
            visible: !opt.tl && !opt.bl && !opt.current
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

    // Single row.
    RowLayout {
        id: row
        visible: !seg.wrap
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        Repeater {
            id: options
            model: seg.wrap ? [] : seg.model
            delegate: Option {
                Layout.fillWidth: seg.stretch
                Layout.fillHeight: true
                tl: index === 0
                bl: index === 0
                tr: index === options.count - 1
                br: index === options.count - 1
            }
        }
    }

    // Wrapped rows: corners follow where the option sits in the block.
    Item {
        id: flowBox
        visible: seg.wrap
        anchors.fill: parent
        anchors.margins: 1
        implicitHeight: flow.implicitHeight
        Flow {
            id: flow
            width: parent.width
            spacing: 0
            property int count: flowRep.count
            function itemAt(i) { return flowRep.itemAt(i) }
            Repeater {
                id: flowRep
                model: seg.wrap ? seg.model : []
                delegate: Option {
                    id: fo
                    width: implicitWidth
                    height: implicitHeight
                    readonly property bool leftEdge: x <= 0
                    readonly property bool topRow: y <= 0
                    readonly property bool bottomRow: y + height >= flow.height
                    readonly property bool rightEdge: index === flowRep.count - 1 || (flowRep.itemAt(index + 1) !== null && flowRep.itemAt(index + 1).y > y)
                    tl: leftEdge && topRow
                    bl: leftEdge && bottomRow
                    tr: rightEdge && topRow
                    br: rightEdge && bottomRow
                }
            }
        }
    }
}
