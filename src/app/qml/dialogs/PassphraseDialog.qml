// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts
import DrStein

StDialog {
    id: dialog
    property string what: ""
    property var callback: null
    property bool allowEmpty: false
    // Every taker of the passphrase starts a job (the unlock probe, a verify, a profile run), and the
    // runner refuses a second one: a passphrase taken now could only be lost, so submit waits.
    readonly property bool busy: JobRunner.running
    title: "Passphrase"
    body: what.length ? "Needed to " + what + "." : "Enter the passphrase."
    onOpened: { field.text = ""; field.forceActiveFocus() }
    // The field's copy goes with the dialog, submitted or not. (QML strings cannot be overwritten in
    // place; what the engine copied while the text was typed lingers until it is collected.)
    onClosed: field.text = ""
    function submit() {
        if ((field.text.length === 0 && !allowEmpty) || busy) return
        var cb = callback; var p = field.text
        field.text = ""
        close()
        if (cb) cb(p); else Workspace.unlock(p)
    }
    StTextField { id: field; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "passphrase"; onAccepted: dialog.submit() }
    Text { Layout.fillWidth: true; text: "Kept in memory for this source only, for this session; forgotten when the source closes, when it turns out wrong, and when Dr Stein quits. Never written anywhere. The field is cleared as soon as it is read; the interface's own copies of the typed text cannot be overwritten and linger until they are collected."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
    Text { Layout.fillWidth: true; visible: dialog.busy; text: "An operation is running; the passphrase can be submitted once it has finished."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.warning; wrapMode: Text.WordWrap }
    actions: [
        StButton { text: dialog.allowEmpty ? "Continue" : "Unlock"; variant: "primary"; enabled: (field.text.length > 0 || dialog.allowEmpty) && !dialog.busy; onClicked: dialog.submit() },
        StButton { text: "Cancel"; onClicked: dialog.close() }
    ]
}
