// SPDX-License-Identifier: MIT
// A surface card with the design system's 1px edge ("elev-sm"). Children go
// into a ColumnLayout with the card's gap.
import QtQuick
import QtQuick.Layouts
import DrStein

Rectangle {
    id: card
    default property alias content: column.data
    property int padding: Theme.space4
    property int gap: Theme.space3
    property int elevation: 1

    radius: Theme.radiusMd
    color: Theme.surface
    border.width: 1
    border.color: elevation >= 3 ? Theme.edge3 : elevation === 2 ? Theme.edge2 : Theme.edge1
    implicitHeight: column.implicitHeight + padding * 2
    implicitWidth: column.implicitWidth + padding * 2
    clip: true

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: card.padding
        spacing: card.gap
    }
}
