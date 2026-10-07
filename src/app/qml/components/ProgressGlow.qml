// SPDX-License-Identifier: MIT
// A 6px track with an accent fill that glows (the mockup's box-shadow).
import QtQuick
import DrStein

Rectangle {
    id: track
    property real fraction: 0
    property bool indeterminate: false
    height: 6
    radius: 3
    color: Theme.sunken
    clip: true
    Rectangle {
        id: fill
        width: track.indeterminate ? parent.width * 0.3 : parent.width * Math.min(1, Math.max(0, track.fraction))
        height: parent.height
        radius: 3
        color: Theme.accent
        // The slide only exists while indeterminate; a determinate bar always starts at 0.
        property real slide: 0
        x: track.indeterminate ? slide : 0
        Behavior on width { enabled: !track.indeterminate; NumberAnimation { duration: 200 } }
        Rectangle { anchors.fill: parent; anchors.margins: -4; radius: 6; color: Theme.accentStep(600); opacity: 0.35; z: -1 }
        SequentialAnimation on slide {
            running: track.indeterminate
            loops: Animation.Infinite
            NumberAnimation { from: -fill.width; to: track.width; duration: 1400; easing.type: Easing.InOutSine }
        }
    }
}
