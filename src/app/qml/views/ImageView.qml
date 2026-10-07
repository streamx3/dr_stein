// SPDX-License-Identifier: MIT
// Create / Restore / Verify / Keys form on the left, the job card on the right.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import DrStein

Item {
    id: view
    property ImageController controller: ImageController {}
    signal confirmRestore()
    signal askPassphrase(string what, var callback)
    signal manageKeys()

    FileDialog {
        id: destDialog
        title: "Image destination"
        fileMode: FileDialog.SaveFile
        currentFolder: "file://" + Settings.lastImageDir
        nameFilters: ["Stein image (*.stein)", "Raw image (*.img *.raw *.dd)"]
        defaultSuffix: view.controller.raw ? "img" : "stein"
        onAccepted: view.controller.setDestinationUrl(selectedFile)
    }
    FileDialog {
        id: restoreSourceDialog
        title: "Image to restore"
        currentFolder: "file://" + Settings.lastImageDir
        nameFilters: ["Disk images (*.stein *.img *.raw *.dd *.qcow2 *.vhd *.vhdx *.vmdk *.vdi *.E01 *.dmg)", "All files (*)"]
        onAccepted: view.controller.setRestoreImageUrl(selectedFile)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space6
        spacing: Theme.space6

        // Left: form. Locked while a job runs; the card on the right keeps Cancel.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            spacing: Theme.space4
            enabled: !Workspace.uiLocked
            opacity: enabled ? 1 : 0.6
            StSegmented {
                model: [{ label: "Create image", value: "create" }, { label: "Restore image", value: "restore" }, { label: "Verify", value: "verify" }, { label: "Keys", value: "keys" }]
                currentValue: view.controller.mode
                onPicked: (v) => view.controller.mode = v
            }
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: formStack.implicitHeight
                contentWidth: width
                clip: true
                ScrollBar.vertical: ScrollBar {}
                StackLayout {
                    id: formStack
                    width: parent.width
                    currentIndex: ["create", "restore", "verify", "keys"].indexOf(view.controller.mode)

                    // ---- Create ---------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space3
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.space4
                            rowSpacing: Theme.space3
                            ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Source"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                RowLayout { Layout.fillWidth: true; spacing: 6
                                    Rectangle { Layout.preferredWidth: 220; height: 30; radius: Theme.radiusMd; color: Theme.bg; border.width: 1; border.color: Theme.edge1
                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9; spacing: 8
                                            Text { Layout.fillWidth: true; text: view.controller.hasSource ? view.controller.sourceTitle : "select a disk or image in the sidebar"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.neutralStep(300); elide: Text.ElideRight }
                                        }
                                    }
                                    StCombo {
                                        Layout.fillWidth: true
                                        enabled: view.controller.hasSource && view.controller.sourceScopes.length > 1
                                        model: view.controller.sourceScopes
                                        textRole: "label"
                                        currentIndex: view.controller.sourceScope
                                        onActivated: (i) => view.controller.sourceScope = i
                                    }
                                }
                                Text { Layout.fillWidth: true; text: view.controller.partitionScope ? "A partition image stores only that partition's bytes plus where it came from (disk identity, table, LBA range, type). It restores into a partition, not onto a whole disk." : "A whole-device image stores the partition table and every partition. It restores onto a whole disk."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
                            }
                            ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Destination"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                RowLayout { Layout.fillWidth: true; spacing: 6
                                    StTextField { Layout.fillWidth: true; mono: true; text: view.controller.destination; onTextEdited: view.controller.destination = text; placeholderText: "/path/to/image.stein" }
                                    StButton { text: "Choose…"; small: true; onClicked: destDialog.open() }
                                }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Format"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                StSegmented { Layout.fillWidth: true; stretch: true; model: [{ label: ".stein", value: false }, { label: "raw .img", value: true }]; currentValue: view.controller.raw; onPicked: (v) => view.controller.raw = v }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Compression"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                StSegmented { Layout.fillWidth: true; stretch: true; enabled: !view.controller.raw; model: [{ label: "LZ4", value: "lz4" }, { label: "None", value: "none" }]; currentValue: view.controller.compression; onPicked: (v) => view.controller.compression = v }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Chunk size"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                StTextField { Layout.fillWidth: true; text: view.controller.chunkSizeText; onTextEdited: view.controller.chunkSizeText = text }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Split every"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                StTextField { Layout.fillWidth: true; enabled: !view.controller.raw; text: view.controller.splitSizeText; placeholderText: "— (single file)"; onTextEdited: view.controller.splitSizeText = text }
                            }
                            ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Bad sectors"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                Row { spacing: 18
                                    StRadio { text: "Zero-fill and count"; checked: view.controller.zeroFillBadSectors; onClicked: view.controller.zeroFillBadSectors = true }
                                    StRadio { text: "Fail"; checked: !view.controller.zeroFillBadSectors; onClicked: view.controller.zeroFillBadSectors = false }
                                }
                            }
                            ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: 8
                                StCheck { Layout.fillWidth: true; enabled: !view.controller.raw; text: "Used blocks only"; sublabel: view.controller.usedBlocksNote; checked: view.controller.usedBlocksOnly; onToggled: view.controller.usedBlocksOnly = checked }
                                StCheck { Layout.fillWidth: true; enabled: !view.controller.raw; text: "Verify after writing (level 3 · full content + SHA-256)"; checked: view.controller.verifyAfter; onToggled: view.controller.verifyAfter = checked }
                                StCheck { Layout.fillWidth: true; enabled: !view.controller.raw; text: "Encrypt"; sublabel: "ChaCha20-Poly1305 · Argon2id"; checked: view.controller.encrypt; onToggled: view.controller.encrypt = checked }
                                StTextField { Layout.fillWidth: true; visible: view.controller.encrypt && !view.controller.raw; echoMode: TextInput.Password; placeholderText: "passphrase"; text: view.controller.passphrase; onTextEdited: view.controller.passphrase = text }
                            }
                            ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: 4
                                Kicker { text: "Notes"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                                StTextField { Layout.fillWidth: true; text: view.controller.notes; placeholderText: "why this image exists"; onTextEdited: view.controller.notes = text }
                            }
                        }
                        Text { Layout.fillWidth: true; visible: view.controller.validationMessage.length > 0; text: view.controller.validationMessage; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.warning; wrapMode: Text.WordWrap }
                        Repeater { model: view.controller.warnings; delegate: Text { required property string modelData; Layout.fillWidth: true; text: "⚠ " + modelData; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textSoft; wrapMode: Text.WordWrap } }
                        RowLayout {
                            spacing: 6
                            Layout.topMargin: Theme.space3
                            StButton { text: view.controller.raw ? "Start copy" : "Start imaging"; variant: "primary"; enabled: view.controller.canStart && !JobRunner.running; onClicked: view.controller.start() }
                            StButton { text: "Dry run"; enabled: view.controller.canStart; onClicked: Workspace.error({ title: "Plan", message: view.controller.planText, hint: "Nothing was written.", severity: "info" }) }
                        }
                    }

                    // ---- Restore --------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space3
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Kicker { text: "Image"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                StTextField { Layout.fillWidth: true; mono: true; text: view.controller.restoreImagePath; onEditingFinished: if (text !== view.controller.restoreImagePath) view.controller.restoreImagePath = text; placeholderText: "a .stein, raw, qcow2, VHD(X), VMDK, VDI, E01 or DMG image" }
                                StButton { text: "Choose\u2026"; small: true; onClicked: restoreSourceDialog.open() }
                            }
                            Text { Layout.fillWidth: true; visible: view.controller.restoreScopeText.length > 0; text: view.controller.restoreScopeText + (view.controller.restoreEncrypted ? (view.controller.restoreUnlocked ? " \u00b7 encrypted, unlocked" : " \u00b7 encrypted: the passphrase is asked before the confirmation") : ""); font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: view.controller.restorePartitionImage ? Theme.accentStep(300) : Theme.textMuted; wrapMode: Text.WordWrap }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Kicker { text: view.controller.restorePartitionImage ? "Target partition (of " + Workspace.title + ")" : "Target"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                            StCombo {
                                id: targetCombo
                                Layout.fillWidth: true
                                model: view.controller.restoreTargets
                                textRole: "name"
                                monoRole: "devicePadded"
                                closedText: currentIndex >= 0 ? model[currentIndex].name + " \u00b7 " + model[currentIndex].fitText : "choose a target"
                                currentIndex: { var m = view.controller.restoreTargets; for (var i = 0; i < m.length; ++i) if (m[i].id === view.controller.restoreTargetId) return i; return -1 }
                                onActivated: (i) => view.controller.restoreTargetId = model[i].id
                            }
                            Text { Layout.fillWidth: true; text: view.controller.restorePartitionImage ? "Only that partition's bytes are overwritten; the table and the other partitions stay. Select the disk in the sidebar to pick one of its partitions." : "Every byte of the target is overwritten. The target is marked red in the sidebar; the confirmation repeats its identity."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
                        }
                        StCheck { text: "Verify the image's checksums before writing anything"; checked: view.controller.restoreVerifyFirst; onToggled: view.controller.restoreVerifyFirst = checked }
                        StCheck { text: "Allow a smaller target (writes what fits; the tail is lost)"; checked: view.controller.restoreAllowSmaller; onToggled: view.controller.restoreAllowSmaller = checked }
                        StCheck { text: "Do not rewrite what the target already holds"; sublabel: "Each chunk is read from the target first and skipped when it already matches. Same result, fewer writes; when the target turns out to differ almost everywhere, comparing pauses and only one chunk in 64 is checked, so the cost stays small."; checked: view.controller.restoreSkipIdentical; onToggled: view.controller.restoreSkipIdentical = checked }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Kicker { text: "Zero ranges of the image"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                            StSegmented {
                                model: [{ label: "Write them", value: "write" }, { label: "Zero free space only", value: "gaps" }, { label: "Skip them", value: "skip" }]
                                currentValue: view.controller.restoreZeros
                                onPicked: (v) => view.controller.restoreZeros = v
                            }
                            Text {
                                Layout.fillWidth: true
                                text: view.controller.restoreZeros === "write" ? "The target becomes byte-identical to the image. Slow on a slow drive: every zero range is written."
                                    : view.controller.restoreZeros === "gaps" ? "Zeros are written only where the image's partition table has free space (gaps, the tail), never inside a partition of any type, so no stale signature survives in a gap. Inside partitions the filesystems come back complete and old bytes stay in their free space. Without a table the library understands, nothing is zeroed."
                                    : "Nothing is written where the image is zero: fastest and gentlest on flash. Old bytes stay recoverable in free space and gaps, and a stale filesystem signature in a gap can confuse other tools."
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap
                            }
                            Text { Layout.fillWidth: true; visible: view.controller.restoreZeroPlanText.length > 0; text: view.controller.restoreZeroPlanText + (view.controller.restoreZerosDefaulted ? (view.controller.restoreZeros === "gaps" ? " \u00b7 default for removable drives" : " \u00b7 default for fixed disks and files") : ""); font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.accentStep(300); wrapMode: Text.WordWrap }
                        }
                        Text { Layout.fillWidth: true; visible: view.controller.restoreMessage.length > 0; text: view.controller.restoreMessage; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.warning; wrapMode: Text.WordWrap }
                        RowLayout { spacing: 6; Layout.topMargin: Theme.space3
                            StButton { text: "Restore\u2026"; variant: "danger"; enabled: view.controller.canRestore && !JobRunner.running; onClicked: view.confirmRestore() }
                        }
                    }

                    // ---- Verify ---------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space3
                        Text { Layout.fillWidth: true; text: view.controller.sourceIsStein ? "Verify " + view.controller.sourceTitle : "Select a .stein image in the sidebar to verify it. Other containers carry no checksums of their own (E01 excepted: its stored MD5 is shown in the identity line)."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.text; wrapMode: Text.WordWrap }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Kicker { text: "Level"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
                            StSegmented { model: [{ label: "1 · structure", value: 1 }, { label: "2 · stored checksums", value: 2 }, { label: "3 · full content + SHA-256", value: 3 }]; currentValue: view.controller.verifyLevel; onPicked: (v) => view.controller.verifyLevel = v }
                            Text { Layout.fillWidth: true; text: "1 takes milliseconds, 2 reads the file without decompressing, 3 decompresses everything and recomputes the SHA-256."; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
                        }
                        RowLayout { spacing: 6; Layout.topMargin: Theme.space3
                            StButton {
                                text: "Verify"; variant: "primary"; enabled: view.controller.canVerify && !JobRunner.running
                                onClicked: {
                                    if (view.controller.sourceEncrypted && view.controller.verifyLevel === 3) view.askPassphrase("verify " + view.controller.sourceTitle, function(p) { view.controller.verify(p) })
                                    else view.controller.verify("")
                                }
                            }
                        }
                    }

                    // ---- Keys -----------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space3
                        Text { Layout.fillWidth: true; visible: view.controller.keysMessage.length > 0; text: view.controller.keysMessage; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.textSoft; wrapMode: Text.WordWrap }
                        Repeater {
                            model: view.controller.keys
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                height: 40
                                radius: Theme.radiusSm
                                color: Theme.sunken
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
                                    Icon { name: "key"; size: 14; color: Theme.accentStep(400) }
                                    Text { text: "slot " + modelData.id; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.text }
                                    Text { text: modelData.label; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textBright }
                                    Text { Layout.fillWidth: true; text: modelData.kdf; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight }
                                }
                            }
                        }
                        RowLayout { spacing: 6; Layout.topMargin: Theme.space3
                            StButton { text: "Manage slots…"; variant: "primary"; enabled: view.controller.sourceEncrypted; onClicked: view.manageKeys() }
                            StButton { text: "Reload"; variant: "ghost"; onClicked: view.controller.reloadKeys() }
                        }
                    }
                }
            }
        }

        JobCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            kind: "image"
            idleKicker: "Ready"
            idleText: Workspace.hasCurrent ? "No image operation running." : "Open a disk or an image first."
        }
    }
}
