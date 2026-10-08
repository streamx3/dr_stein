// SPDX-License-Identifier: MIT
// The ".seg" control: linked options, rounded at the two ends and square where they
// join (per-corner radii, Qt 6.7), the current one ringed in the accent with the same
// corners. With twoLine, a narrow layout shrinks the options and their labels break
// into two lines instead of the row overflowing.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import DrStein

Rectangle {
    id: seg
    property var model: []          // [{label, value}] or ["A", "B"]
    property var currentValue: null
    property bool stretch: false    // options share the width
    property bool twoLine: false    // labels may wrap to two lines when the row is squeezed
    property bool small: true
    signal picked(var value)

    readonly property int r: Theme.radiusMd - 1
    // What one row asks for with every label on one line, and the least it can take with
    // labels on two lines; a layout may place the control anywhere between.
    readonly property real oneRowWidth: {
        var w = 2
        for (var i = 0; i < options.count; ++i) w += options.itemAt(i) ? options.itemAt(i).implicitWidth : 0
        return w
    }
    readonly property real minRowWidth: {
        var w = 2
        for (var i = 0; i < options.count; ++i) w += options.itemAt(i) ? options.itemAt(i).Layout.minimumWidth : 0
        return w
    }

    radius: Theme.radiusMd
    color: Theme.surface
    border.width: 1
    border.color: Theme.edge1
    implicitHeight: row.implicitHeight + 2
    implicitWidth: oneRowWidth

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        Repeater {
            id: options
            model: seg.model
            delegate: Rectangle {
                id: opt
                required property var modelData
                required property int index
                readonly property string label: typeof modelData === "object" ? modelData.label : modelData
                readonly property var value: typeof modelData === "object" ? modelData.value : modelData
                readonly property bool current: seg.currentValue === value
                readonly property bool first: index === 0
                readonly property bool last: index === options.count - 1
                readonly property int pad: 24
                FontMetrics { id: fm; font: txt.font }
                // The narrowest width at which the label still fits on two lines: the best break
                // between words, each half measured.
                readonly property real twoLineWidth: {
                    var words = opt.label.split(" ")
                    var best = fm.advanceWidth(opt.label)
                    for (var k = 1; k < words.length; ++k)
                        best = Math.min(best, Math.max(fm.advanceWidth(words.slice(0, k).join(" ")), fm.advanceWidth(words.slice(k).join(" "))))
                    return Math.ceil(best) + 2
                }
                implicitWidth: Math.ceil(fm.advanceWidth(opt.label)) + pad
                implicitHeight: txt.implicitHeight + (seg.small ? 12 : 14)
                Layout.fillWidth: seg.stretch || seg.twoLine
                Layout.fillHeight: true
                Layout.preferredWidth: implicitWidth
                Layout.minimumWidth: seg.twoLine ? twoLineWidth + pad : implicitWidth
                color: current ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08) : (ma.containsMouse ? Theme.sunken : "transparent")
                topLeftRadius: first ? seg.r : 0
                bottomLeftRadius: first ? seg.r : 0
                topRightRadius: last ? seg.r : 0
                bottomRightRadius: last ? seg.r : 0
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
                    width: seg.twoLine ? opt.width - opt.pad : implicitWidth
                    text: opt.label
                    font.family: Theme.fontFamily
                    font.pixelSize: seg.small ? Theme.fontSmall : Theme.fontSize
                    font.weight: Font.Medium
                    color: opt.current ? Theme.accent : Theme.textBright
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: seg.twoLine ? Text.WordWrap : Text.NoWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
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
