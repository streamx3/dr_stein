// SPDX-License-Identifier: MIT
// Current/preview bars · preview tree · operation stack. Nothing is written
// before Apply, which confirms with the device identity.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Item {
    id: view
    property PartitionEditor editor: PartitionEditor {}
    property int selectedPartition: -1
    signal addPartition()
    signal editPartition(int index)
    signal confirmApply()
    signal newTable()

    function report(e) { if (e.message !== undefined) Workspace.error(e) }
    readonly property var selectedInfo: {
        var ps = editor.partitions
        for (var i = 0; i < ps.length; ++i) if (ps[i].index === selectedPartition) return ps[i]
        return null
    }

    EmptyState {
        anchors.fill: parent
        visible: !view.editor.available
        icon_: "layers"
        title: "Cannot edit here"
        message: view.editor.unavailableReason
        actions: [
            StButton { text: "Try again"; small: true; icon_: "refresh"; onClicked: view.editor.reload() },
            StButton { visible: Workspace.details.mountpoint.length > 0; text: "Unmount " + Workspace.details.mountpoint; small: true; icon_: "eject"; onClicked: Workspace.unmountSelected() }
        ]
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space6
        spacing: Theme.space4
        visible: view.editor.available

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Text { text: "Current"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; Layout.preferredWidth: 70 }
            SegmentBar { Layout.fillWidth: true; compact: true; segments: view.editor.baseSegments }
            Text { text: "Preview"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.accentStep(300); Layout.preferredWidth: 70 }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 22
                radius: Theme.radiusSm
                color: "transparent"
                border.width: view.editor.pendingCount > 0 ? 1 : 0
                border.color: Theme.accentStep(700)
                SegmentBar { anchors.fill: parent; anchors.margins: view.editor.pendingCount > 0 ? 1 : 0; compact: true; segments: view.editor.previewSegments }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space6

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                enabled: !Workspace.uiLocked
                opacity: enabled ? 1 : 0.6
                ColumnHeader {
                    Layout.fillWidth: true
                    columns: [{ label: "Preview tree" }, { label: "Type", width: 150 }, { label: "Size", width: 110, align: "right" }, { label: "Change", width: 96 }]
                }
                ListView {
                    id: previewList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: view.editor.previewRows
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: prow
                        required property var modelData
                        required property int index
                        readonly property int partIndex: modelData.deleted ? -1 : modelData.index
                        width: previewList.width
                        height: 32
                        radius: Theme.radiusSm
                        color: (modelData.changed ? Theme.accentStep(900) : "transparent")
                        border.width: view.selectedPartition >= 0 && prow.partIndex === view.selectedPartition ? 1 : 0
                        border.color: Theme.accent
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 12
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle { width: 8; height: 8; radius: 2; color: prow.modelData.isFree ? Theme.sunken : Theme.partitionColor(prow.modelData.colorIndex); border.width: prow.modelData.isFree ? 1 : 0; border.color: Theme.neutralStep(700) }
                                Text { Layout.fillWidth: true; text: prow.modelData.name; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.strikeout: prow.modelData.deleted; color: prow.modelData.deleted ? Theme.textMuted : Theme.text; elide: Text.ElideRight }
                            }
                            Text { Layout.preferredWidth: 150; Layout.minimumWidth: 150; Layout.maximumWidth: 150; text: prow.modelData.typeCode; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 110; Layout.minimumWidth: 110; Layout.maximumWidth: 110; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight; text: prow.modelData.sizeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.neutralStep(300) }
                            Text { Layout.preferredWidth: 96; Layout.minimumWidth: 96; Layout.maximumWidth: 96; elide: Text.ElideRight; text: prow.modelData.change; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: prow.modelData.changed ? Theme.accentStep(300) : Theme.textMuted }
                        }
                        MouseArea { anchors.fill: parent; onClicked: view.selectedPartition = prow.partIndex }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space4
                    Layout.leftMargin: 10
                    spacing: 6
                    StButton { text: "Add partition…"; small: true; enabled: view.editor.hasTable && !JobRunner.running; onClicked: view.addPartition() }
                    StButton { text: "Edit…"; small: true; enabled: view.selectedPartition > 0 && view.editor.hasTable && !JobRunner.running; onClicked: view.editPartition(view.selectedPartition) }
                    StButton { text: "Delete"; small: true; enabled: view.selectedPartition > 0 && view.editor.hasTable && !JobRunner.running; onClicked: { view.report(view.editor.deletePartition(view.selectedPartition)); view.selectedPartition = -1 } }
                    StButton { text: "Repair table"; small: true; enabled: view.editor.canRepair && !JobRunner.running; onClicked: view.report(view.editor.repairTable()) }
                    StButton { text: "Wipe signatures"; small: true; enabled: view.selectedPartition > 0 && !JobRunner.running; onClicked: view.report(view.editor.wipe(view.selectedPartition)) }
                    StButton { text: "New table…"; variant: "ghost"; small: true; enabled: !JobRunner.running; onClicked: view.newTable() }
                }
                Text { Layout.leftMargin: 10; Layout.topMargin: 4; text: view.editor.freeSpaceHint; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
            }

            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                spacing: Theme.space4
                StCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    gap: Theme.space3
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                            Kicker { text: "Operation stack" }
                            Text { text: view.editor.summary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH3; font.weight: Theme.headingWeight; color: Theme.text }
                        }
                        Text { text: "overlay · nothing written"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: view.editor.pending
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: inner.implicitHeight + 16
                            radius: Theme.radiusSm
                            color: Theme.sunken
                            RowLayout {
                                id: inner
                                anchors.fill: parent; anchors.margins: 8; anchors.leftMargin: 10; spacing: 10
                                Text { text: modelData.n; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textDim; Layout.alignment: Qt.AlignTop }
                                ColumnLayout { Layout.fillWidth: true; spacing: 1
                                    Text { Layout.fillWidth: true; text: modelData.title; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; wrapMode: Text.WordWrap }
                                    Text { Layout.fillWidth: true; text: modelData.detail; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WrapAnywhere }
                                }
                                StTag { visible: modelData.destructive; text: "destructive"; variant: "danger"; Layout.alignment: Qt.AlignTop }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "Changed byte ranges"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                        Text { Layout.fillWidth: true; text: view.editor.changedRanges; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textSoft; wrapMode: Text.WrapAnywhere }
                    }
                    RowLayout {
                        spacing: 6
                        StButton { text: "Apply " + view.editor.pendingCount + (view.editor.pendingCount === 1 ? " operation…" : " operations…"); variant: view.editor.destructive ? "danger" : "primary"; enabled: view.editor.pendingCount > 0 && !JobRunner.running; onClicked: view.confirmApply() }
                        StButton { text: "Undo last"; enabled: view.editor.pendingCount > 0 && !JobRunner.running; onClicked: view.editor.undoLast() }
                        StButton { text: "Clear"; variant: "ghost"; enabled: view.editor.pendingCount > 0 && !JobRunner.running; onClicked: view.editor.clear() }
                    }
                }
                JobCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    visible: JobRunner.kind === "partitions" || JobRunner.lastKind === "partitions"
                    kind: "partitions"
                }
            }
        }
    }
}
