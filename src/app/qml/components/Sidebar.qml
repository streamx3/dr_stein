// SPDX-License-Identifier: MIT
// Disks and images, grouped, with "Open image file…" at the bottom.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Item {
    id: sidebar
    signal openImageRequested()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        enabled: !Workspace.uiLocked
        opacity: enabled ? 1 : 0.6
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Workspace.sources
            topMargin: Theme.space4
            bottomMargin: Theme.space4
            leftMargin: Theme.space3
            rightMargin: Theme.space3
            spacing: 2
            ScrollBar.vertical: ScrollBar {}
            section.property: "group"
            section.criteria: ViewSection.FullString
            section.delegate: Kicker {
                required property string section
                width: list.width - list.leftMargin - list.rightMargin
                height: 24
                verticalAlignment: Text.AlignBottom
                leftPadding: 10
                bottomPadding: 6
                text: section
            }
            delegate: Rectangle {
                id: row
                required property var model
                required property int index
                width: list.width - list.leftMargin - list.rightMargin
                height: 46
                radius: Theme.radiusMd
                readonly property bool isTarget: model.target
                color: isTarget ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.16) : model.selected ? Theme.surface : (rowMouse.containsMouse ? Theme.surface : "transparent")
                border.width: isTarget ? 1 : 0
                border.color: Theme.danger
                Rectangle { width: 2; height: parent.height - 14; x: 0; y: 7; radius: 1; color: row.isTarget ? Theme.danger : Theme.accent; visible: row.model.selected || row.isTarget }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 10
                    Icon { name: row.model.icon; size: 18; color: row.isTarget ? Theme.danger : row.model.selected ? Theme.accent : Theme.textMuted }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text { Layout.fillWidth: true; text: row.model.name; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.weight: Font.Medium; color: row.isTarget ? Theme.danger : Theme.text; elide: Text.ElideRight }
                            Icon { visible: row.model.locked; name: "lock"; size: 11; color: Theme.textMuted }
                            Text { text: row.model.sizeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            StTag { visible: row.isTarget; text: "restore target"; variant: "danger"; implicitHeight: 16 }
                            Text { Layout.fillWidth: true; text: row.model.subtitle; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: row.isTarget ? Theme.danger : Theme.textMuted; elide: Text.ElideRight }
                        }
                    }
                    IconButton {
                        visible: row.model.kind === "image" && (rowMouse.containsMouse || hovered)
                        icon_: "close"; iconSize: 11; implicitWidth: 20; implicitHeight: 20; tip: "Close this image"
                        onClicked: Workspace.closeImage(row.model.id)
                    }
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    z: -1
                    onClicked: Workspace.select(row.model.id)
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: open.implicitHeight + Theme.space3 * 2
            StButton {
                id: open
                anchors.fill: parent
                anchors.margins: Theme.space3
                text: "Open image file…"
                icon_: "image"
                onClicked: sidebar.openImageRequested()
            }
        }
    }
    FadeDivider { vertical: true; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right }
}
