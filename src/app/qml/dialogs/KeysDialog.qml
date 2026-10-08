// SPDX-License-Identifier: MIT
// Add or remove passphrase slots of an encrypted .stein image.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import DrStein

StDialog {
    id: dialog
    property ImageController controller: null
    title: "Key slots"
    body: "Every slot unlocks the same image. Adding or removing a slot rewrites the key area in place; the data is untouched."
    dialogWidth: 520
    onOpened: { current.text = ""; fresh.text = ""; label.text = ""; if (controller) controller.reloadKeys() }
    Repeater {
        model: dialog.controller ? dialog.controller.keys : []
        delegate: Rectangle {
            id: slotRow
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: 36
            radius: Theme.radiusSm
            color: Theme.sunken
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
                Text { text: "slot " + slotRow.modelData.id; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.text }
                Text { Layout.fillWidth: true; text: (slotRow.modelData.label.length ? slotRow.modelData.label + " · " : "") + slotRow.modelData.kdf; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight }
                StButton { text: "Remove"; variant: "danger"; small: true; enabled: current.text.length > 0; onClicked: { var e = dialog.controller.removeKey(current.text, slotRow.modelData.id); if (e.message !== undefined) Workspace.error(e) } }
            }
        }
    }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "Current passphrase (any slot)"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        StTextField { id: current; Layout.fillWidth: true; echoMode: TextInput.Password }
    }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "New passphrase"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        RowLayout { Layout.fillWidth: true; spacing: 6
            StTextField { id: fresh; Layout.fillWidth: true; echoMode: TextInput.Password }
            StTextField { id: label; Layout.preferredWidth: 140; placeholderText: "label (optional)" }
            StButton { text: "Add slot"; variant: "primary"; small: true; enabled: current.text.length > 0 && fresh.text.length > 0; onClicked: { var e = dialog.controller.addKey(current.text, fresh.text, label.text); if (e.message !== undefined) Workspace.error(e); else fresh.text = "" } }
        }
    }
    actions: [ StButton { text: "Close"; variant: "primary"; onClicked: dialog.close() } ]
}
