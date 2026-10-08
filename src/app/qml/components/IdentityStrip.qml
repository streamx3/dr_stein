// SPDX-License-Identifier: MIT
// Title · mono path · identity line, with the table / health / read-only tags.
import QtQuick
import QtQuick.Layouts
import DrStein

Item {
    implicitHeight: col.implicitHeight + Theme.space4 + Theme.space3
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.topMargin: Theme.space4
        anchors.bottomMargin: Theme.space3
        spacing: Theme.space4
        // The text column elides before the tags are touched: the tags keep their
        // natural width, the path gives way first, then the title.
        ColumnLayout {
            id: col
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 1
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.minimumWidth: Math.min(implicitWidth, 120)
                    Layout.maximumWidth: implicitWidth
                    text: Workspace.hasCurrent || Workspace.hasCurrentError ? Workspace.title : "No disk selected"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontH1
                    font.weight: Theme.headingWeight
                    color: Theme.text
                    elide: Text.ElideRight
                }
                Text {
                    text: Workspace.path
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: Math.min(implicitWidth, 420)
                }
                Text {
                    visible: Workspace.busy
                    text: "probing…"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.accentStep(300)
                }
            }
            Text {
                Layout.fillWidth: true
                text: Workspace.hasCurrent || Workspace.hasCurrentError ? Workspace.identityLine : "Pick a disk in the sidebar or open an image file. Everything opens read-only."
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
                elide: Text.ElideRight
            }
        }
        Row {
            spacing: 6
            visible: Workspace.hasCurrent
            Layout.minimumWidth: implicitWidth
            StTag { text: Workspace.tableText; variant: "neutral" }
            StTag { text: Workspace.healthText; variant: Workspace.healthLevel === "ok" ? "accent" : Workspace.healthLevel }
            StTag { text: Workspace.readOnly ? "read-only" : "writable"; variant: "outline" }
        }
    }
}
