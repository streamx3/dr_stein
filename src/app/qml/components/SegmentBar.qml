// SPDX-License-Identifier: MIT
// The byte-proportional bar: one block per partition or free region, widths
// by length with a minimum so tiny partitions stay clickable.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import DrStein

Item {
    id: bar
    property var segments: []     // [{label, length, isFree, colorIndex}]
    property int selected: -1
    property bool compact: false
    property bool highlightChanges: false
    signal clicked(int index)

    implicitHeight: compact ? 22 : 34
    readonly property real total: {
        var t = 0
        for (var i = 0; i < segments.length; ++i) t += Number(segments[i].length)
        return Math.max(1, t)
    }
    // Minimum widths eat into the proportional space; compute the remainder once.
    readonly property real minW: compact ? 6 : 10
    readonly property real spare: {
        var fixed = 0
        for (var i = 0; i < segments.length; ++i) {
            var w = width * Number(segments[i].length) / total
            if (w < minW) fixed += minW - w
        }
        return Math.max(0, width - fixed - 2 * Math.max(0, segments.length - 1))
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: Theme.sunken
        visible: bar.segments.length === 0
    }

    Row {
        anchors.fill: parent
        spacing: 2
        Repeater {
            model: bar.segments
            delegate: Rectangle {
                id: seg
                required property var modelData
                required property int index
                readonly property bool isFree: modelData.isFree === true
                readonly property bool current: bar.selected === index
                readonly property real natural: bar.spare * Number(modelData.length) / bar.total
                width: Math.max(bar.minW, natural)
                height: bar.height
                radius: index === 0 || index === bar.segments.length - 1 ? Theme.radiusSm : 1
                color: isFree ? Theme.sunken : Theme.partitionColor(modelData.colorIndex)
                border.width: current ? 1 : 0
                border.color: Theme.accent
                clip: true

                // Free space: diagonal hatching.
                Canvas {
                    id: hatch
                    anchors.fill: parent
                    visible: seg.isFree
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Theme.neutralStep(800)
                        ctx.lineWidth = 2
                        for (var x = -height; x < width + height; x += 6) {
                            ctx.beginPath(); ctx.moveTo(x, height); ctx.lineTo(x + height, 0); ctx.stroke()
                        }
                    }
                    Connections { target: Theme; function onDarkChanged() { hatch.requestPaint() } }
                }
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !bar.compact || (seg.modelData.label !== undefined && seg.modelData.label.length > 0 && parent.width > 40)
                    text: seg.modelData.label !== undefined ? seg.modelData.label : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: bar.compact ? Theme.fontMicro : Theme.fontTiny
                    color: seg.current ? Theme.accentStep(100) : Theme.partitionTextColor(seg.isFree ? -1 : seg.modelData.colorIndex)
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onClicked: bar.clicked(seg.index)
                    ToolTip.visible: containsMouse && seg.modelData.label !== undefined && seg.modelData.label.length > 0
                    ToolTip.text: seg.modelData.label !== undefined ? seg.modelData.label : ""
                    ToolTip.delay: 500
                }
            }
        }
    }
}
