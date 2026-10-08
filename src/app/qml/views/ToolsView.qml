// SPDX-License-Identifier: MIT
// Surface scan (read-only) and the capacity test (destructive, confirmed).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import DrStein

Item {
    id: view
    property ToolsController tools: ToolsController {}
    property bool quick: true
    property bool restoreZeros: true
    signal confirmErase()

    FileDialog {
        id: reportDialog
        title: "Save scan report"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Text (*.txt)"]
        defaultSuffix: "txt"
        onAccepted: { var e = view.tools.saveScanReport(selectedFile); if (e.length) Workspace.error({ title: "Could not save", message: e, severity: "error" }) }
    }

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space6
        contentHeight: grid.implicitHeight
        contentWidth: width
        clip: true
        ScrollBar.vertical: ScrollBar {}
        GridLayout {
            id: grid
            width: parent.width
            columns: width > 900 ? 2 : 1
            columnSpacing: Theme.space4
            rowSpacing: Theme.space4

            // Surface scan.
            StCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                gap: Theme.space3
                enabled: !Workspace.uiLocked
                opacity: enabled ? 1 : 0.6
                ColumnLayout {
                    spacing: 1
                    Kicker { text: "Read-only" }
                    Text { text: "Surface scan"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH2; font.weight: Theme.headingWeight; color: Theme.text }
                    Text { Layout.fillWidth: true; text: view.tools.hasSource ? "Reads every sector of " + view.tools.targetPath + " and lists unreadable regions. Writes nothing." : "Select a disk or an image."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft; wrapMode: Text.WordWrap }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: cells.implicitHeight + 20
                    radius: Theme.radiusSm
                    color: Theme.sunken
                    Grid {
                        id: cells
                        anchors.fill: parent; anchors.margins: 10
                        columns: 64
                        spacing: 2
                        readonly property real cell: (width - 63 * 2) / 64
                        Repeater {
                            model: view.tools.scanCells
                            delegate: Rectangle {
                                required property int modelData
                                required property int index
                                width: cells.cell; height: cells.cell; radius: 1
                                color: modelData === 2 ? Theme.danger : modelData === 1 ? (index % 7 === 3 ? Theme.accentStep(700) : Theme.accentStep(800)) : Theme.neutralStep(800)
                            }
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3; columnSpacing: 8; rowSpacing: 2
                    visible: view.tools.scanDone
                    Text { text: "Scanned"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                    Text { text: "Unreadable"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                    Text { text: "Read"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                    Text { text: view.tools.scannedText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text }
                    Text { text: view.tools.unreadableText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: view.tools.scanHealthy ? Theme.ok : Theme.warning }
                    Text { text: view.tools.scanRateText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text }
                }
                Rectangle {
                    Layout.fillWidth: true
                    visible: view.tools.badLines.length > 0
                    implicitHeight: bad.implicitHeight + 16
                    radius: Theme.radiusSm
                    color: Theme.sunken
                    Text { id: bad; anchors.fill: parent; anchors.margins: 8; text: view.tools.badLines.join("\n"); font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textSoft; wrapMode: Text.WrapAnywhere }
                }
                RowLayout {
                    spacing: 6
                    StButton { text: view.tools.scanDone ? "Scan again" : "Scan"; variant: "primary"; enabled: view.tools.canScan; onClicked: view.tools.surfaceScan() }
                    StButton { text: "Save report…"; enabled: view.tools.scanDone; onClicked: reportDialog.open() }
                }
            }

            // Capacity test.
            StCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                gap: Theme.space3
                enabled: !Workspace.uiLocked
                opacity: enabled ? 1 : 0.6
                ColumnLayout {
                    spacing: 1
                    Kicker { text: "Destructive"; tint: Theme.danger }
                    Text { text: "Capacity test (fake-flash)"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH2; font.weight: Theme.headingWeight; color: Theme.text }
                    Text { Layout.fillWidth: true; text: "Writes a pattern across the whole device, reads it back, finds where data stops matching or wraps around, estimates real capacity and measures speed. Everything on the device is lost."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft; wrapMode: Text.WordWrap }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2; columnSpacing: Theme.space3; rowSpacing: 4
                    Kicker { text: "Target"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                    Kicker { text: "Mode"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: Theme.radiusMd; color: Theme.bg; border.width: 1; border.color: Theme.edge1
                        RowLayout { anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9; spacing: 8
                            Text { text: view.tools.hasSource ? view.tools.targetTitle : "—"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.text; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: view.tools.targetPath; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted; elide: Text.ElideMiddle }
                        }
                    }
                    StSegmented { Layout.fillWidth: true; stretch: true; model: [{ label: "Quick · 1/16", value: true }, { label: "Full", value: false }]; currentValue: view.quick; onPicked: (v) => view.quick = v }
                }
                StCheck { text: "Restore zeros afterwards"; checked: view.restoreZeros; onToggled: view.restoreZeros = checked }
                RowLayout {
                    visible: view.tools.testDone
                    spacing: 10
                    StTag { text: view.tools.testVerdict; variant: view.tools.testHealthy ? "ok" : "danger" }
                    Text { Layout.fillWidth: true; text: view.tools.testDetail + " · " + view.tools.testSpeedText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; wrapMode: Text.WordWrap }
                }
                RowLayout {
                    spacing: 10
                    StButton { text: "Run test…"; variant: "danger"; enabled: view.tools.canTest; onClicked: view.confirmErase() }
                    Text { Layout.fillWidth: true; text: view.tools.testBlockedReason.length ? view.tools.testBlockedReason : "refuses while any volume on " + view.tools.targetPath + " is mounted"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
                }
            }

            JobCard {
                Layout.fillWidth: true
                Layout.columnSpan: grid.columns
                Layout.minimumHeight: 180
                kind: "tools"
                idleText: "No test running."
            }
        }
    }
}
