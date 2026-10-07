// SPDX-License-Identifier: MIT
// The shell: top bar, sidebar, identity strip, the seven views, dialogs.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import DrStein

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "Dr Stein" + (Workspace.hasCurrent ? " — " + Workspace.title : "")
    color: Theme.bg
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize

    readonly property var viewIds: ["topology", "hex", "browse", "image", "profiles", "tools", "partitions"]

    // Client-side title bar (the editor look): macOS and Windows extend the client
    // area into the system title bar and keep the system frame; Linux goes
    // frameless with our own buttons and resize bands. Read once at startup.
    readonly property bool customChrome: Settings.customTitleBar
    readonly property bool framelessChrome: customChrome && Qt.platform.os !== "osx" && Qt.platform.os !== "windows"
    flags: !customChrome ? Qt.Window
         : framelessChrome ? (Qt.Window | Qt.FramelessWindowHint)
         : (Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint)
    Component.onCompleted: { if (customChrome) WindowChrome.attach(window); Qt.callLater(measureChrome) }

    FileDialog {
        id: openImageDialog
        title: "Open image file"
        currentFolder: "file://" + Settings.lastImageDir
        nameFilters: ["Disk images (*.stein *.img *.raw *.dd *.qcow2 *.vhd *.vhdx *.vmdk *.vdi *.E01 *.e01 *.dmg *.iso)", "All files (*)"]
        onAccepted: Workspace.openImage(selectedFile)
    }

    Shortcut { sequence: StandardKey.Open; onActivated: openImageDialog.open() }
    Shortcut { sequence: StandardKey.Refresh; onActivated: Workspace.refresh() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsDialog.open() }
    Repeater {
        model: 7
        Item {
            required property int index
            Shortcut { sequence: "Ctrl+" + (index + 1); onActivated: Workspace.view = window.viewIds[index] }
        }
    }

    // With the expanded client area, ApplicationWindow keeps its content below the
    // title-bar band (the safe area). The tab bar is meant to live in that band, so the
    // layout is pulled back up by the same amount and the bar grows to fill it.
    readonly property int titleBand: customChrome ? window.SafeArea.margins.top : 0
    // Measured after the window exists: where the traffic lights are, so the bar centres on them.
    property var chromeMetrics: ({})
    function measureChrome() { if (customChrome) chromeMetrics = WindowChrome.metrics(window) }
    onVisibilityChanged: Qt.callLater(measureChrome)

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: -window.titleBand
        spacing: 0
        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: window.chromeMetrics.lightsCenterY !== undefined ? Math.max(implicitHeight, 2 * window.chromeMetrics.lightsCenterY) : implicitHeight
            leftInsetOverride: window.chromeMetrics.leftInset !== undefined ? window.chromeMetrics.leftInset : -1
            window: window
            customChrome: window.customChrome
            onOpenSettings: settingsDialog.open()
            onOpenMounts: mountsDialog.open()
        }
        SplitView {
            id: split
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 4; color: "transparent" }
            onResizingChanged: if (!resizing && sidebar.width >= 200) Settings.sidebarWidth = Math.round(sidebar.width)
            Sidebar {
                id: sidebar
                SplitView.preferredWidth: Math.max(220, Settings.sidebarWidth)
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 420
                onOpenImageRequested: openImageDialog.open()
            }
            ColumnLayout {
                SplitView.fillWidth: true
                spacing: 0
                IdentityStrip { Layout.fillWidth: true }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: Math.max(0, window.viewIds.indexOf(Workspace.view))
                    TopologyView {
                        onUnlockRequested: { passphraseDialog.what = "unlock " + Workspace.title; passphraseDialog.callback = null; passphraseDialog.open() }
                        onMountRequested: Workspace.view = "browse"
                    }
                    HexView {
                        id: hexView
                        onConfirmWrite: {
                            confirmDialog.title = "Write " + hexView.inspector.dirtyCount + " byte(s) to the device?"
                            confirmDialog.body = "The edited bytes replace the structure's bytes on " + Workspace.path + ". The device is then re-probed."
                            confirmDialog.facts = [{ key: "Device", value: Workspace.title + " · " + Workspace.sizeText, mono: false }, { key: "Path", value: Workspace.path }, { key: "Structure", value: hexView.inspector.label, mono: false }, { key: "Ranges", value: hexView.inspector.dirtyRanges }]
                            confirmDialog.expectedText = Workspace.isDisk ? Workspace.sourceInfo(Workspace.currentId).kernelName : ""
                            confirmDialog.confirmLabel = "Write"
                            confirmDialog.onConfirm = function() { var e = hexView.inspector.write(); if (e.message !== undefined) Workspace.error(e) }
                            confirmDialog.open()
                        }
                    }
                    BrowseView { id: browseView }
                    ImageView {
                        id: imageView
                        onConfirmRestore: {
                            // Order: passphrase (checked against the image), then the typed confirmation, then the write.
                            function confirmThenRestore() {
                                var t = imageView.controller.restoreTarget
                                confirmDialog.title = t.isPartition ? "Restore into partition " + t.partitionIndex + " of " + t.title + "?" : "Restore onto " + t.title + "?"
                                confirmDialog.body = t.isPartition ? "Every byte of that partition is overwritten with the image; the partition table and the other partitions stay. This cannot be undone." : "Every byte on the target is overwritten with the image. This cannot be undone."
                                confirmDialog.facts = [{ key: "Image", value: imageView.controller.restoreImagePath }, { key: "Scope", value: imageView.controller.restoreScopeText, mono: false }, { key: "Target", value: (t.isPartition ? t.targetName + " \u00b7 " + t.targetSizeText : t.title + " \u00b7 " + t.sizeText) + (t.removable ? " \u00b7 removable" : ""), mono: false }, { key: "Path", value: t.path }, { key: "Serial", value: t.serial.length ? t.serial : "\u2014" }, { key: "Zero ranges", value: imageView.controller.restoreSkipZeros ? "skipped: old bytes stay there" : "written", mono: false }]
                                confirmDialog.expectedText = t.kind === "disk" ? t.kernelName : ""
                                confirmDialog.confirmLabel = "Restore"
                                confirmDialog.onConfirm = function() { imageView.controller.restore("") }
                                confirmDialog.open()
                            }
                            function askPassphrase() {
                                passphraseDialog.what = "unlock " + imageView.controller.restoreImagePath.split("/").pop()
                                passphraseDialog.allowEmpty = false
                                passphraseDialog.callback = function(p) {
                                    if (imageView.controller.checkRestorePassphrase(p)) confirmThenRestore()
                                    else { Workspace.error({ title: "Wrong passphrase", message: "The passphrase does not open this image.", hint: "", severity: "warning" }); askPassphrase() }
                                }
                                passphraseDialog.open()
                            }
                            if (imageView.controller.restoreEncrypted && !imageView.controller.restoreUnlocked) askPassphrase()
                            else confirmThenRestore()
                        }
                        onAskPassphrase: (what, cb) => { passphraseDialog.what = what; passphraseDialog.callback = cb; passphraseDialog.allowEmpty = false; passphraseDialog.open() }
                        onManageKeys: { keysDialog.controller = imageView.controller; keysDialog.open() }
                    }
                    ProfilesView {
                        id: profilesView
                        onNewProfile: newProfileDialog.open()
                        onAskPassphrase: (what, cb) => { passphraseDialog.what = what; passphraseDialog.callback = cb; passphraseDialog.allowEmpty = false; passphraseDialog.open() }
                        onConfirmRestore: (row) => {
                            var card = ProfilesModel.get(row)
                            confirmDialog.title = "Restore " + card.name + "?"
                            confirmDialog.body = "The image is written over the disk the profile matched. The library verifies the image first, per the profile's policy."
                            confirmDialog.facts = [{ key: "Disk", value: card.diskText, mono: false }, { key: "Match", value: card.selectorText }, { key: "Image", value: card.imagePath }, { key: "Policy", value: card.policyText, mono: false }]
                            confirmDialog.expectedText = ""
                            confirmDialog.confirmLabel = "Restore"
                            confirmDialog.onConfirm = function() { profilesView.run(row, "restore", false) }
                            confirmDialog.open()
                        }
                    }
                    ToolsView {
                        id: toolsView
                        onConfirmErase: {
                            confirmDialog.title = "Erase and test this device?"
                            confirmDialog.body = "Every byte on the device will be overwritten." + (toolsView.restoreZeros ? " Zeros are written back afterwards." : " The test pattern is left in place.")
                            confirmDialog.facts = [{ key: "Device", value: toolsView.tools.targetIdentity, mono: false }, { key: "Path", value: toolsView.tools.targetPath }, { key: "Contains", value: toolsView.tools.targetContents, mono: false }]
                            confirmDialog.expectedText = toolsView.tools.expectedConfirm
                            confirmDialog.confirmLabel = "Erase and test"
                            confirmDialog.onConfirm = function() { var e = toolsView.tools.capacityTest(toolsView.tools.expectedConfirm, toolsView.quick, !toolsView.restoreZeros); if (e.message !== undefined) Workspace.error(e) }
                            confirmDialog.open()
                        }
                    }
                    PartitionsView {
                        id: partitionsView
                        onAddPartition: { partitionDialog.editor = partitionsView.editor; partitionDialog.editIndex = -1; partitionDialog.current = null; partitionDialog.open() }
                        onEditPartition: (index) => { partitionDialog.editor = partitionsView.editor; partitionDialog.editIndex = index; partitionDialog.current = partitionsView.selectedInfo; partitionDialog.open() }
                        onNewTable: {
                            confirmDialog.title = "Create a new partition table?"
                            confirmDialog.body = "Choose the scheme. Existing partitions become unreachable once applied; nothing is written until Apply."
                            confirmDialog.facts = [{ key: "Device", value: partitionsView.editor.deviceIdentity, mono: false }]
                            confirmDialog.expectedText = ""
                            confirmDialog.confirmLabel = "GPT"
                            confirmDialog.onConfirm = function() { partitionsView.report(partitionsView.editor.newTable("gpt")) }
                            confirmDialog.extraLabel = "MBR"
                            confirmDialog.onExtra = function() { partitionsView.report(partitionsView.editor.newTable("mbr")) }
                            confirmDialog.open()
                        }
                        onConfirmApply: {
                            confirmDialog.title = "Apply " + partitionsView.editor.pendingCount + " operation(s)?"
                            confirmDialog.body = partitionsView.editor.destructive ? "At least one operation overwrites data. The changes are written in one pass and the device is re-probed." : "Metadata only: the table is rewritten in one ordered pass and the device is re-probed."
                            confirmDialog.facts = [{ key: "Device", value: partitionsView.editor.deviceIdentity, mono: false }, { key: "Pending", value: partitionsView.editor.summary, mono: false }, { key: "Ranges", value: partitionsView.editor.changedRanges }]
                            confirmDialog.expectedText = partitionsView.editor.isRealDevice ? Workspace.sourceInfo(Workspace.currentId).kernelName : ""
                            confirmDialog.confirmLabel = "Apply"
                            confirmDialog.danger = partitionsView.editor.destructive
                            confirmDialog.onConfirm = function() { partitionsView.editor.apply() }
                            confirmDialog.open()
                        }
                    }
                }
            }
        }
    }

    FrameResizer { visible: window.framelessChrome; window: window; z: 50 }

    // Dialogs.
    PassphraseDialog { id: passphraseDialog }
    ConfirmDialog { id: confirmDialog; onClosed: { extraLabel = ""; onExtra = null; danger = true } }
    PartitionDialog { id: partitionDialog }
    NewProfileDialog { id: newProfileDialog }
    SettingsDialog { id: settingsDialog }
    MountsDialog { id: mountsDialog }
    KeysDialog { id: keysDialog }
    ToastHost { z: 100 }

    Connections {
        target: Workspace
        function onPassphraseNeeded(what) { passphraseDialog.what = "unlock " + what; passphraseDialog.callback = null; passphraseDialog.allowEmpty = false; passphraseDialog.open() }
    }
}
