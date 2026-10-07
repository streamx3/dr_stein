// SPDX-License-Identifier: MIT
// The running / finished operation: phase, bar, stats, then the report.
// Shown by every view that starts jobs of its `kind`.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

StCard {
    id: card
    property string kind: ""
    property string idleKicker: "Ready"
    property string idleText: "Nothing running."
    readonly property bool mine: JobRunner.kind === kind || JobRunner.lastKind === kind
    readonly property bool running: JobRunner.running && JobRunner.kind === kind
    readonly property bool finished: !JobRunner.running && JobRunner.hasResult && JobRunner.lastKind === kind
    gap: Theme.space4

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Kicker {
            text: card.running ? "Running · " + JobRunner.title
                 : card.finished ? (JobRunner.lastOk ? "Done · " : "Failed · ") + JobRunner.lastTitle
                 : card.idleKicker
            tint: card.finished && !JobRunner.lastOk ? Theme.danger : Theme.textMuted
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: card.running ? (JobRunner.phase.length ? JobRunner.phase : "Starting…")
                    : card.finished ? (JobRunner.lastOk ? (JobRunner.lastSummary.length ? JobRunner.lastSummary : "finished") : JobRunner.lastError.message)
                    : card.idleText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH2
                font.weight: Theme.headingWeight
                color: card.finished && !JobRunner.lastOk ? Theme.danger : Theme.text
                wrapMode: Text.WordWrap
            }
            Text {
                visible: card.running && JobRunner.percentText.length > 0
                text: JobRunner.percentText
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSize
                color: Theme.accentStep(300)
            }
        }
    }
    ProgressGlow {
        Layout.fillWidth: true
        visible: card.running
        fraction: JobRunner.fraction
        indeterminate: JobRunner.indeterminate
    }
    RowLayout {
        Layout.fillWidth: true
        visible: card.running
        Text { text: JobRunner.doneText + (JobRunner.totalText.length ? " / " + JobRunner.totalText : ""); font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; Layout.fillWidth: true }
        Text { text: JobRunner.rateText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; Layout.fillWidth: true }
        Text { text: JobRunner.etaText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; Layout.fillWidth: true }
        Text {
            text: "Cancel"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            color: Theme.accentStep(300)
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: JobRunner.cancel() }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: card.finished && !JobRunner.lastOk && JobRunner.lastError.hint !== undefined && JobRunner.lastError.hint.length > 0
        text: JobRunner.lastError.hint !== undefined ? JobRunner.lastError.hint : ""
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        color: Theme.textSoft
        wrapMode: Text.WordWrap
    }
    // Live messages while running, the report afterwards.
    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 60
        clip: true
        spacing: 2
        visible: card.running || card.finished
        model: card.running ? JobRunner.messages : JobRunner.report
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: row
            required property var model
            required property int index
            readonly property bool isReport: row.model.title !== undefined
            width: list.width
            implicitHeight: content.implicitHeight + 8
            RowLayout {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8 + (row.isReport ? row.model.depth * 18 : 0)
                anchors.rightMargin: 8
                spacing: 10
                Rectangle {
                    width: 8; height: 8; radius: 4
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 5
                    color: !row.isReport ? Theme.accent
                         : row.model.status === "success" ? Theme.ok
                         : row.model.status === "warning" ? Theme.warning
                         : row.model.status === "error" ? Theme.danger
                         : row.model.status === "running" ? Theme.accent : Theme.neutralStep(800)
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: row.isReport ? row.model.title : String(row.model.modelData)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: row.isReport && row.model.status === "pending" ? Theme.textMuted : Theme.textBright
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: row.isReport && row.model.detail.length > 0
                        text: row.isReport ? row.model.detail : ""
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: row.isReport && row.model.lines.length > 0
                        text: row.isReport ? row.model.lines : ""
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontTiny
                        color: Theme.textSoft
                        wrapMode: Text.WrapAnywhere
                    }
                }
                Text {
                    visible: row.isReport && row.model.duration.length > 0
                    text: row.isReport ? row.model.duration : ""
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontTiny
                    color: Theme.textDim
                    Layout.alignment: Qt.AlignTop
                }
            }
        }
    }
}
