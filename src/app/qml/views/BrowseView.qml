// SPDX-License-Identifier: MIT
// Directory tree · entries · preview card, over a read-only Reader.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import DrStein

Item {
    id: view
    property FileBrowser browser: FileBrowser {}
    property int copyRow: -1

    FolderDialog {
        id: copyDialog
        title: "Copy out to folder"
        onAccepted: { if (view.copyRow >= 0) view.browser.copyOut(view.copyRow, selectedFolder); else view.browser.copyAll(selectedFolder) }
    }
    function copyEntry(row) { view.copyRow = row; copyDialog.open() }

    Connections {
        target: view.browser
        function onMounted(mountpoint) { Workspace.error({ title: "Mounted", message: "Readable at " + mountpoint + " until you unmount it (top bar).", hint: "", severity: "info" }) }
    }

    EmptyState {
        anchors.fill: parent
        visible: !view.browser.available
        icon_: "folder"
        title: "No readable filesystem selected"
        message: view.browser.unavailableReason
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space4
        spacing: Theme.space3
        visible: view.browser.available
        enabled: !view.browser.busy && !Workspace.uiLocked
        opacity: enabled ? 1 : 0.6

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3
            Rectangle {
                implicitHeight: 26
                implicitWidth: crumbs.implicitWidth + 20
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.edge1
                Row {
                    id: crumbs
                    anchors.centerIn: parent
                    spacing: 4
                    Text { text: view.browser.title; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.accentStep(300) }
                    Text { text: view.browser.fsName; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textDim }
                    Repeater {
                        model: view.browser.breadcrumbs
                        delegate: Row {
                            required property var modelData
                            spacing: 4
                            Text { text: "/"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textDim; visible: modelData.index > 0 || true }
                            Text {
                                text: modelData.index === 0 ? "" : modelData.name
                                visible: modelData.index > 0
                                font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.text
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: view.browser.jumpTo(modelData.index) }
                            }
                        }
                    }
                }
            }
            StTag { text: "read-only"; variant: "outline" }
            StCombo {
                visible: view.browser.subvolumes.length > 0
                small: true
                Layout.preferredWidth: 220
                model: [{ label: "default volume", volume: "", snapshot: "" }].concat(view.browser.subvolumes.map(function(s) {
                    return { label: s.kind + " " + s.name + (s.parent.length ? " of " + s.parent : ""), volume: s.kind === "snapshot" ? s.parent : s.name, snapshot: s.kind === "snapshot" ? s.name : "" }
                }))
                textRole: "label"
                onActivated: (i) => { var m = model[i]; view.browser.openSubvolume(m.volume, m.snapshot) }
            }
            Item { Layout.fillWidth: true }
            StButton { text: "Mount instead"; small: true; enabled: view.browser.canMount && !JobRunner.running; onClicked: view.browser.mount() }
            StButton { text: "Copy all…"; small: true; onClicked: view.copyEntry(-1) }
            StButton { text: "Copy out…"; variant: "primary"; small: true; enabled: view.browser.hasSelection; onClicked: view.copyEntry(view.browser.selectedIndex) }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space4

            // Directory tree.
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.edge1
                clip: true
                TreeView {
                    id: dirTree
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: view.browser.tree
                    selectionBehavior: TableView.SelectRows
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: treeRow
                        required property TreeView treeView
                        required property bool isTreeNode
                        required property bool expanded
                        required property bool hasChildren
                        required property int depth
                        required property int row
                        required property var model
                        implicitWidth: dirTree.width - 12
                        implicitHeight: 24
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: treeRow.model.isCurrent ? Theme.accentStep(900) : (treeMouse.containsMouse ? Theme.sunken : "transparent")
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8 + treeRow.depth * 14
                            spacing: 6
                            Text { width: 10; text: treeRow.hasChildren ? (treeRow.expanded ? "▾" : "▸") : ""; font.pixelSize: Theme.fontMicro; color: Theme.textDim }
                            Icon { name: "folder"; size: 14; color: Theme.accentStep(400) }
                            Text { Layout.fillWidth: true; text: treeRow.model.name; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: treeRow.model.isCurrent ? Theme.accentStep(100) : Theme.textBright; elide: Text.ElideRight }
                        }
                        MouseArea {
                            id: treeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: { view.browser.goTo(treeRow.model.path); if (treeRow.hasChildren && !treeRow.expanded) treeRow.treeView.expand(treeRow.row) }
                            onDoubleClicked: treeRow.treeView.toggleExpanded(treeRow.row)
                        }
                    }
                    onRowsChanged: if (rows === 1) Qt.callLater(function() { dirTree.expand(0) })
                }
            }

            // Entries.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.edge1
                clip: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    ColumnHeader {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        leftPad: 12; rightPad: 12; spacing: 10
                        columns: [{ label: "Name" }, { label: "Size", width: 80, align: "right" }, { label: "Modified", width: 130 }, { label: "Mode", width: 90 }]
                    }
                    ListView {
                        id: entries
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: view.browser.entries
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: erow
                            required property var model
                            required property int index
                            width: entries.width
                            height: 26
                            color: model.selected ? Theme.accentStep(900) : (erowMouse.containsMouse ? Theme.sunken : "transparent")
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 8
                                    Icon { name: erow.model.kind === "dir" ? "folder" : erow.model.kind === "link" ? "link" : "file"; size: 14; color: erow.model.kind === "dir" ? Theme.accentStep(400) : Theme.textMuted }
                                    Text { text: erow.model.name; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; elide: Text.ElideRight; Layout.maximumWidth: 360; Layout.minimumWidth: 0; Layout.fillWidth: erow.model.linkTarget.length === 0 }
                                    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: erow.model.linkTarget; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight }
                                }
                                Text { Layout.preferredWidth: 80; Layout.minimumWidth: 80; Layout.maximumWidth: 80; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight; text: erow.model.sizeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft }
                                Text { Layout.preferredWidth: 130; Layout.minimumWidth: 130; Layout.maximumWidth: 130; elide: Text.ElideRight; text: erow.model.mtimeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft }
                                Text { Layout.preferredWidth: 90; Layout.minimumWidth: 90; Layout.maximumWidth: 90; elide: Text.ElideRight; text: erow.model.modeText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                            }
                            MouseArea {
                                id: erowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: view.browser.select(erow.index)
                                onDoubleClicked: view.browser.activate(erow.index)
                            }
                        }
                    }
                }
            }

            // Preview.
            StCard {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                gap: Theme.space3
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Kicker { text: "Preview · " + (view.browser.showHex && view.browser.previewHex.length ? "hex" : view.browser.previewKind) }
                    Text { Layout.fillWidth: true; text: view.browser.hasSelection ? view.browser.selectedName : "nothing selected"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH3; font.weight: Theme.headingWeight; color: Theme.text; elide: Text.ElideMiddle }
                    Text { Layout.fillWidth: true; text: view.browser.selectedInfo; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WrapAnywhere }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSm
                    color: Theme.sunken
                    clip: true
                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 10
                        contentWidth: previewText.implicitWidth
                        contentHeight: previewText.implicitHeight
                        clip: true
                        visible: !(view.browser.previewKind === "image" && !view.browser.showHex)
                        ScrollBar.vertical: ScrollBar {}
                        ScrollBar.horizontal: ScrollBar {}
                        TextEdit {
                            id: previewText
                            readOnly: true
                            selectByMouse: true
                            text: view.browser.showHex || view.browser.previewKind === "hex" ? view.browser.previewHex
                                : view.browser.previewKind === "text" ? view.browser.previewText
                                : view.browser.previewNote
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontTiny
                            color: Theme.neutralStep(300)
                            selectionColor: Theme.accentStep(700)
                        }
                    }
                    Image {
                        anchors.fill: parent
                        anchors.margins: 10
                        visible: view.browser.previewKind === "image" && !view.browser.showHex
                        source: visible ? view.browser.previewImage : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                }
                Text { visible: view.browser.previewNote.length > 0 && view.browser.previewKind !== "empty"; text: view.browser.previewNote; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; Layout.fillWidth: true; elide: Text.ElideRight }
                KeyValueGrid {
                    Layout.fillWidth: true
                    fontSize: Theme.fontTiny
                    rows: [{ key: "mtime", value: view.browser.mtimeText, mono: false }, { key: "crtime", value: view.browser.crtimeText, mono: false }, { key: "SHA-256", value: view.browser.hashText, mono: true }]
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    StButton { text: "Hash"; small: true; enabled: view.browser.hasSelection && !JobRunner.running; onClicked: view.browser.hash() }
                    StButton { text: view.browser.showHex ? "Text" : "Hex"; small: true; enabled: view.browser.hasSelection; onClicked: view.browser.showHex = !view.browser.showHex }
                    Item { Layout.fillWidth: true }
                    StButton { text: "Copy out…"; variant: "primary"; small: true; enabled: view.browser.hasSelection; onClicked: view.copyEntry(view.browser.selectedIndex) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Text { text: view.browser.statusText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
            Text { text: "lazy: nothing read until a directory opens"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
            Item { Layout.fillWidth: true }
            Text { text: view.browser.busy ? JobRunner.title + "…" : "double-click a folder to enter it · Copy out writes to a folder of your choice"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
        }
    }
}
