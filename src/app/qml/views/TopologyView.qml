// SPDX-License-Identifier: MIT
// Byte-proportional bar · tree table · details card. (Mockup: Topology.)
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Item {
    id: view
    signal unlockRequested()
    signal mountRequested()

    EmptyState {
        anchors.fill: parent
        visible: !Workspace.hasCurrent
        icon_: Workspace.hasCurrentError ? "warning" : "disk"
        title: Workspace.hasCurrentError ? Workspace.currentError.title : "Nothing open"
        message: Workspace.hasCurrentError ? Workspace.currentError.message + (Workspace.currentError.hint ? "\n\n" + Workspace.currentError.hint : "")
               : "Select a disk in the sidebar, or open an image file. Opening never writes."
        actions: [
            StButton {
                visible: Workspace.hasCurrentError && Workspace.currentError.fullDiskAccess === true && Workspace.canOpenPrivacySettings()
                text: "Open Privacy & Security\u2026"; variant: "primary"; small: true; icon_: "gear"
                onClicked: Workspace.openPrivacySettings()
            },
            StButton {
                visible: Workspace.hasCurrentError && Workspace.currentError.fullDiskAccess === true && Workspace.canOpenPrivacySettings()
                text: "Show app in Finder"; small: true; icon_: "folder"
                onClicked: Workspace.revealAppInFinder()
            },
            StButton {
                visible: Workspace.hasCurrentError && Workspace.currentError.fullDiskAccess === true
                text: "Try again"; small: true; icon_: "refresh"
                onClicked: Workspace.reopenCurrent()
            },
            StButton {
                visible: Workspace.hasCurrentError && Workspace.currentError.needsElevation === true && !Workspace.imageLocked && Workspace.canRelaunchElevated()
                text: "Relaunch elevated\u2026"; variant: "primary"; small: true; icon_: "unlock"
                onClicked: Workspace.relaunchElevated()
            },
            StButton {
                visible: Workspace.hasCurrentError && Workspace.currentError.needsElevation === true && Workspace.imageLocked
                text: "Enter passphrase…"; variant: "primary"; small: true
                onClicked: view.unlockRequested()
            }
        ]
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space6
        spacing: Theme.space4
        visible: Workspace.hasCurrent
        enabled: !Workspace.uiLocked
        opacity: enabled ? 1 : 0.6

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            SegmentBar {
                Layout.fillWidth: true
                segments: Workspace.segments
                selected: Workspace.selectedSegment
                onClicked: (i) => Workspace.selectSegment(i)
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "LBA 0"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontMicro; color: Theme.textDim; Layout.fillWidth: true }
                Text { text: Workspace.lastLbaText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontMicro; color: Theme.textDim }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space6

            // Tree table.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                ColumnHeader {
                    Layout.fillWidth: true
                    columns: [{ label: "Node" }, { label: "Content", width: 170 }, { label: "Size", width: 90, align: "right" }, { label: "Health", width: 96 }]
                }
                ListView {
                    id: tree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Workspace.topology
                    currentIndex: Workspace.topology.selectedRow
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property var model
                        required property int index
                        width: tree.width
                        height: 32
                        radius: Theme.radiusSm
                        color: model.selected ? Theme.surface : (rowMouse.containsMouse ? Theme.surface : "transparent")
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 12
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.leftMargin: row.model.depth * 18
                                spacing: 8
                                Rectangle {
                                    width: 8; height: 8; radius: 2
                                    color: row.model.isMetadata ? Theme.neutralStep(600)
                                         : row.model.kindLabel === "Free" ? Theme.sunken
                                         : row.model.segment >= 0 ? Theme.partitionColor(Workspace.segments[row.model.segment] !== undefined ? Workspace.segments[row.model.segment].colorIndex : row.model.segment)
                                         : Theme.textMuted
                                    border.width: row.model.kindLabel === "Free" ? 1 : 0
                                    border.color: Theme.neutralStep(700)
                                }
                                Text {
                                    text: row.model.depth === 0 ? Workspace.title : row.model.name
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 260
                                }
                                Text { text: row.model.kindLabel; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                                Text { visible: row.model.osDevice.length > 0; text: row.model.osDevice; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.accentStep(400) }
                                StTag { visible: row.model.mountpoint.length > 0; text: "mounted"; variant: "outline" }
                                Icon { visible: row.model.isLocked; name: "lock"; size: 11; color: Theme.warning }
                                Item { Layout.fillWidth: true }
                            }
                            Text { Layout.preferredWidth: 170; Layout.minimumWidth: 170; Layout.maximumWidth: 170; text: row.model.content; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 90; Layout.minimumWidth: 90; Layout.maximumWidth: 90; elide: Text.ElideRight; text: row.model.sizeText; horizontalAlignment: Text.AlignRight; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.neutralStep(300) }
                            HealthDot { Layout.preferredWidth: 96; Layout.minimumWidth: 96; Layout.maximumWidth: 96; clip: true; level: row.model.health; text: row.model.healthText }
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: Workspace.selectRow(row.index)
                            onDoubleClicked: { Workspace.selectRow(row.index); if (row.model.canBrowse) Workspace.view = "browse"; else if (row.model.canInspect) Workspace.view = "hex" }
                        }
                    }
                }
            }

            // Details.
            StCard {
                id: details
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                gap: Theme.space3
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Kicker { text: Workspace.details.kindLabel }
                    Text { Layout.fillWidth: true; text: Workspace.details.title; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH1; font.weight: Theme.headingWeight; color: Theme.text; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: Workspace.details.regionText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted; wrapMode: Text.WrapAnywhere }
                }
                UsageBar {
                    Layout.fillWidth: true
                    visible: Workspace.details.hasUsed
                    label: Workspace.details.usedLabel
                    fraction: Workspace.details.usedFraction
                    valueText: Math.round(Workspace.details.usedFraction * 100) + "%"
                }
                Text {
                    visible: Workspace.details.hasUsed && Workspace.details.usedText.length > 0
                    text: Workspace.details.usedText
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted
                }
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: detailsCol.implicitHeight
                    contentWidth: width
                    ScrollBar.vertical: ScrollBar {}
                    ColumnLayout {
                        id: detailsCol
                        width: parent.width
                        spacing: Theme.space3
                        KeyValueGrid { Layout.fillWidth: true; rows: Workspace.details.rows }
                        Repeater {
                            model: Workspace.details.notes
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: noteRow.implicitHeight + 16
                                radius: Theme.radiusSm
                                color: Theme.sunken
                                RowLayout {
                                    id: noteRow
                                    anchors.fill: parent; anchors.margins: 8; spacing: 8
                                    Text { text: modelData.code; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.healthColor(modelData.severity); Layout.alignment: Qt.AlignTop }
                                    Text { Layout.fillWidth: true; text: modelData.message; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                        Repeater {
                            model: Workspace.details.subvolumes
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.kind + " \"" + modelData.name + "\"" + (modelData.parent.length ? " of \"" + modelData.parent + "\"" : "") + (modelData.note.length ? " · " + modelData.note : "")
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textSoft; wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    StButton { text: "Inspect bytes"; variant: "primary"; small: true; enabled: Workspace.details.canInspect; onClicked: Workspace.view = "hex" }
                    StButton { text: "Browse files"; small: true; enabled: Workspace.details.canBrowse; onClicked: Workspace.view = "browse" }
                    StButton { text: "Repair"; small: true; enabled: Workspace.details.canRepair; onClicked: Workspace.view = "partitions" }
                    StButton { visible: Workspace.details.mountpoint.length === 0; text: "Mount"; small: true; enabled: Workspace.details.canMount && !JobRunner.running; onClicked: view.mountRequested() }
                    StButton { visible: Workspace.details.mountpoint.length > 0; text: "Unmount " + Workspace.details.mountpoint; small: true; icon_: "eject"; enabled: !JobRunner.running; onClicked: Workspace.unmountSelected() }
                    StButton { text: "Unlock…"; small: true; icon_: "unlock"; visible: Workspace.details.isLockedContainer; onClicked: view.unlockRequested() }
                    StButton { text: "Compute usage"; variant: "ghost"; small: true; visible: !Workspace.details.hasUsed && Workspace.details.canInspect && Workspace.details.kindLabel !== "Device" && Workspace.details.kindLabel !== "Metadata" && Workspace.details.kindLabel !== "Free"; enabled: !JobRunner.running; onClicked: Workspace.computeUsage() }
                }
            }
        }
    }
}
