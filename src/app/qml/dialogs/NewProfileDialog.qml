// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import DrStein

StDialog {
    id: dialog
    title: "New profile from " + Workspace.title
    body: "The profile stores the disk's identity (serial, WWN, model, size) and where its image lives. The image is created by the first backup."
    dialogWidth: 520
    onOpened: { name.text = Workspace.title; desc.text = ""; image.text = ""; name.forceActiveFocus() }
    FileDialog {
        id: imageDialog
        title: "Where the image will live"
        fileMode: FileDialog.SaveFile
        currentFolder: "file://" + Settings.lastImageDir
        nameFilters: ["Stein image (*.stein)"]
        defaultSuffix: "stein"
        onAccepted: image.text = selectedFile.toString().replace("file://", "")
    }
    function submit() {
        if (image.text.length === 0) { Workspace.error({ title: "Choose an image path", message: "A profile needs a place for its image.", severity: "warning" }); return }
        var e = ProfilesModel.newFromCurrent("file://" + image.text, name.text, desc.text, usedOnly.checked, encrypt.checked)
        if (e.message !== undefined) { Workspace.error(e); return }
        close()
    }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "Name"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        StTextField { id: name; Layout.fillWidth: true }
    }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "Description"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        StTextField { id: desc; Layout.fillWidth: true; placeholderText: "Whole NVMe to the backup HDD before every big upgrade." }
    }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "Image"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        RowLayout { Layout.fillWidth: true; spacing: 6
            StTextField { id: image; Layout.fillWidth: true; mono: true; placeholderText: "/backups/workstation.stein" }
            StButton { text: "Choose…"; small: true; onClicked: imageDialog.open() }
        }
    }
    StCheck { id: usedOnly; checked: true; text: "Used blocks only"; sublabel: "Free space of ext4, FAT, exFAT, NTFS and HFS+ volumes is skipped." }
    StCheck { id: encrypt; checked: false; text: "Encrypt the image"; sublabel: "The passphrase is asked at backup and restore time and never stored." }
    actions: [
        StButton { text: "Create profile"; variant: "primary"; onClicked: dialog.submit() },
        StButton { text: "Cancel"; onClicked: dialog.close() }
    ]
}
