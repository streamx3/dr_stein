// SPDX-License-Identifier: MIT
// Add or edit a partition. Empty fields keep the library's defaults (add:
// largest free region, 1 MiB aligned; edit: unchanged).
import QtQuick
import QtQuick.Layouts
import DrStein

StDialog {
    id: dialog
    property PartitionEditor editor: null
    property int editIndex: -1               // -1: add
    property var current: null               // the partition being edited
    readonly property bool editing: editIndex > 0
    title: editing ? "Edit partition " + editIndex : "Add partition"
    body: editing ? "Empty fields keep their value. Bounds in sectors (2048s) or sizes (512 MiB)." : "Empty start and size take the largest free region, aligned to 1 MiB."
    dialogWidth: 520
    onOpened: {
        start.text = ""; size.text = ""; end.text = ""; name.text = editing && current ? current.name : ""; wipe.checked = !editing
        var code = editing && current ? current.typeCode : (editor && editor.scheme === "GPT" ? "8300" : editor && editor.scheme === "MBR" ? "0x83" : "")
        type.text = code
        start.forceActiveFocus()
    }
    function submit() {
        var e = editing ? editor.editPartition(editIndex, start.text, size.text, end.text, type.text, name.text, name.text !== (current ? current.name : ""))
                        : editor.addPartition(start.text, size.text, end.text, type.text, name.text, wipe.checked)
        if (e.message !== undefined) { Workspace.error(e); return }
        close()
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2; columnSpacing: Theme.space4; rowSpacing: Theme.space3
        ColumnLayout { Layout.fillWidth: true; spacing: 4
            Kicker { text: "Start"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
            StTextField { id: start; Layout.fillWidth: true; mono: true; placeholderText: dialog.editing && dialog.current ? dialog.current.startLba + "s" : "auto" }
        }
        ColumnLayout { Layout.fillWidth: true; spacing: 4
            Kicker { text: "Size"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
            StTextField { id: size; Layout.fillWidth: true; mono: true; placeholderText: dialog.editing && dialog.current ? dialog.current.sizeText : "to the end of the region" }
        }
        ColumnLayout { Layout.fillWidth: true; spacing: 4
            Kicker { text: "End (wins over size)"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
            StTextField { id: end; Layout.fillWidth: true; mono: true; placeholderText: dialog.editing && dialog.current ? dialog.current.endLba + "s" : "" }
        }
        ColumnLayout { Layout.fillWidth: true; spacing: 4
            Kicker { text: "Type"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
            RowLayout { Layout.fillWidth: true; spacing: 6
                StTextField { id: type; Layout.fillWidth: true; mono: true; placeholderText: "8300, EF00, a GUID, 0x83…" }
                StCombo {
                    Layout.preferredWidth: 170
                    small: true
                    model: dialog.editor ? dialog.editor.typeChoices : []
                    textRole: "label"
                    displayText: "pick…"
                    onActivated: (i) => type.text = model[i].code
                }
            }
        }
        ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: 4
            Kicker { text: "Name"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
            StTextField { id: name; Layout.fillWidth: true; placeholderText: dialog.editor && dialog.editor.scheme === "MBR" ? "MBR has no names" : "optional"; enabled: !(dialog.editor && dialog.editor.scheme === "MBR"); onAccepted: dialog.submit() }
        }
    }
    StCheck { id: wipe; visible: !dialog.editing; text: "Wipe stale filesystem signatures in the new range"; sublabel: "Marks the operation destructive; the bytes are zeroed when applied." }
    actions: [
        StButton { text: dialog.editing ? "Update" : "Add"; variant: "primary"; onClicked: dialog.submit() },
        StButton { text: "Cancel"; onClicked: dialog.close() }
    ]
}
