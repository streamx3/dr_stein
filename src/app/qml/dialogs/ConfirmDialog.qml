// SPDX-License-Identifier: MIT
// Destructive confirmations repeat the device identity; some demand the
// kernel name typed back (the capacity test, restores to real disks).
import QtQuick
import QtQuick.Layouts
import DrStein

StDialog {
    id: dialog
    property var facts: []            // [{key, value, mono}]
    property string expectedText: ""  // empty: no typed confirmation
    property string confirmLabel: "Confirm"
    property bool danger: true
    property var onConfirm: null
    property string extraLabel: ""
    property var onExtra: null
    onOpened: { typed.text = ""; if (expectedText.length) typed.forceActiveFocus() }
    readonly property bool ready: expectedText.length === 0 || typed.text === expectedText
    function go() { if (!ready) return; var cb = onConfirm; close(); if (cb) cb() }

    Rectangle {
        Layout.fillWidth: true
        visible: dialog.facts.length > 0
        implicitHeight: grid.implicitHeight + 24
        radius: Theme.radiusMd
        color: Theme.sunken
        KeyValueGrid { id: grid; anchors.fill: parent; anchors.margins: 12; rows: dialog.facts; fontSize: Theme.fontSize }
    }
    Text { Layout.fillWidth: true; visible: dialog.expectedText.length > 0; text: "Type the device name to confirm."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.textSoft }
    StTextField { id: typed; Layout.fillWidth: true; visible: dialog.expectedText.length > 0; mono: true; placeholderText: dialog.expectedText; onAccepted: dialog.go() }
    actions: [
        StButton { text: dialog.confirmLabel; variant: dialog.danger ? "danger" : "primary"; enabled: dialog.ready; onClicked: dialog.go() },
        StButton { visible: dialog.extraLabel.length > 0; text: dialog.extraLabel; variant: "primary"; enabled: dialog.ready; onClicked: { var cb = dialog.onExtra; dialog.close(); if (cb) cb() } },
        StButton { text: "Cancel"; onClicked: dialog.close() }
    ]
}
