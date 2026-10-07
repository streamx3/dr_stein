// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts
import DrStein

StDialog {
    id: dialog
    property string what: ""
    property var callback: null
    property bool allowEmpty: false
    title: "Passphrase"
    body: what.length ? "Needed to " + what + "." : "Enter the passphrase."
    onOpened: { field.text = ""; field.forceActiveFocus() }
    function submit() {
        if (field.text.length === 0 && !allowEmpty) return
        var cb = callback; var p = field.text
        close()
        if (cb) cb(p); else Workspace.unlock(p)
    }
    StTextField { id: field; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "passphrase"; onAccepted: dialog.submit() }
    Text { Layout.fillWidth: true; text: "Kept in memory for this session only; never written anywhere."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
    actions: [
        StButton { text: dialog.allowEmpty ? "Continue" : "Unlock"; variant: "primary"; enabled: field.text.length > 0 || dialog.allowEmpty; onClicked: dialog.submit() },
        StButton { text: "Cancel"; onClicked: dialog.close() }
    ]
}
