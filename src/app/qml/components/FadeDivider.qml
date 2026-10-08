// SPDX-License-Identifier: MIT
// Nocturne rules fade to transparent at both ends (48px) instead of stopping.
import QtQuick
import DrStein

Rectangle {
    id: divider
    property bool vertical: false
    property int fade: 48
    implicitHeight: vertical ? 100 : 1
    implicitWidth: vertical ? 1 : 100
    color: "transparent"
    gradient: Gradient {
        orientation: divider.vertical ? Gradient.Vertical : Gradient.Horizontal
        GradientStop { position: 0.0; color: "transparent" }
        GradientStop { position: Math.min(0.5, divider.fade / Math.max(1, divider.vertical ? divider.height : divider.width)); color: Theme.divider }
        GradientStop { position: 1 - Math.min(0.5, divider.fade / Math.max(1, divider.vertical ? divider.height : divider.width)); color: Theme.divider }
        GradientStop { position: 1.0; color: "transparent" }
    }
}
