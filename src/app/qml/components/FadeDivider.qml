// SPDX-License-Identifier: MIT
// Nocturne rules fade to transparent at both ends (48px) instead of stopping.
import QtQuick
import DrStein

Rectangle {
    property bool vertical: false
    property int fade: 48
    implicitHeight: vertical ? 100 : 1
    implicitWidth: vertical ? 1 : 100
    color: "transparent"
    gradient: Gradient {
        orientation: vertical ? Gradient.Vertical : Gradient.Horizontal
        GradientStop { position: 0.0; color: "transparent" }
        GradientStop { position: Math.min(0.5, fade / Math.max(1, vertical ? height : width)); color: Theme.divider }
        GradientStop { position: 1 - Math.min(0.5, fade / Math.max(1, vertical ? height : width)); color: Theme.divider }
        GradientStop { position: 1.0; color: "transparent" }
    }
}
