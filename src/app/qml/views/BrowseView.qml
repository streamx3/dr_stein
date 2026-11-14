// SPDX-License-Identifier: MIT
// Directory tree · entries · preview card, over a read-only Reader. The entries list
// holds the keyboard; the preview follows the selection.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import DrStein

Item {
    id: view
    property FileBrowser browser: FileBrowser {}
    property int copyRow: -1

    readonly property bool current: Workspace.view === "browse"
    readonly property bool listFocused: entries.activeFocus
    // What the preview pane shows: "off" (the explanation and the button), "message" (a line
    // of words), "image", "hex" or "text".
    readonly property string paneMode: {
        var kind = view.browser.previewKind
        if (kind === "off" || (kind === "none" && !view.browser.previewEnabled)) return "off"
        if (view.browser.showHex && view.browser.previewHex.length > 0) return "hex"
        return kind === "image" || kind === "hex" || kind === "text" ? kind : "message"
    }

    // The keyboard. The list's Keys handler calls keyPressed(); a DRSTEIN_SCRIPT, which cannot
    // press keys, calls the same functions.
    function pageRows() { return Math.max(1, Math.floor(entries.height / entries.rowHeight) - 1) }
    function moveSelection(delta) { view.browser.moveSelection(delta) }
    // True when the key was one of the list's. Nothing moves while a job holds the view or another
    // view is shown: a hidden item can still be the window's focus item.
    function keyPressed(key, modifiers, autoRepeat) {
        if (!layout.enabled || !view.visible) return false
        if (modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return false
        switch (key) {
        case Qt.Key_Up: view.moveSelection(-1); return true
        case Qt.Key_Down: view.moveSelection(1); return true
        case Qt.Key_PageUp: view.moveSelection(-view.pageRows()); return true
        case Qt.Key_PageDown: view.moveSelection(view.pageRows()); return true
        case Qt.Key_Home: view.browser.selectFirst(); return true
        case Qt.Key_End: view.browser.selectLast(); return true
        // A held key does not walk through the directories.
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Right:
            if (!autoRepeat) view.browser.activateSelected()
            return true
        case Qt.Key_Backspace: case Qt.Key_Left:
            if (!autoRepeat) view.browser.up()
            return true
        }
        return false
    }
    // The list takes the keyboard, but never from a dialog: then it takes it when the dialog lets go.
    property bool focusWanted: false
    function focusList() {
        for (var item = view.Window.activeFocusItem; item; item = item.parent) {
            if (item === view.Overlay.overlay) { view.focusWanted = true; return }
        }
        view.focusWanted = false
        entries.forceActiveFocus()
    }
    Connections {
        target: view.Window.window
        enabled: view.focusWanted
        function onActiveFocusItemChanged() { if (view.current) view.focusList(); else view.focusWanted = false }
    }
    onCurrentChanged: if (current) focusList()
    Component.onCompleted: if (current) focusList()

    FolderDialog {
        id: copyDialog
        title: "Copy out to folder"
        onAccepted: { if (view.copyRow >= 0) view.browser.copyOut(view.copyRow, selectedFolder); else view.browser.copyAll(selectedFolder) }
    }
    function copyEntry(row) { view.copyRow = row; view.focusList(); copyDialog.open() }

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
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space4
        spacing: Theme.space3
        visible: view.browser.available
        enabled: !view.browser.busy && !Workspace.uiLocked
        opacity: enabled ? 1 : 0.6
        onEnabledChanged: if (enabled && view.current) view.focusList()

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
                            id: crumb
                            required property var modelData
                            spacing: 4
                            Text { text: "/"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textDim; visible: crumb.modelData.index > 0 || true }
                            Text {
                                text: crumb.modelData.index === 0 ? "" : crumb.modelData.name
                                visible: crumb.modelData.index > 0
                                font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.text
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { view.browser.jumpTo(crumb.modelData.index); view.focusList() } }
                            }
                        }
                    }
                }
            }
            StTag { text: "read-only"; variant: "outline"; boxed: true }   // same shape and height as the breadcrumb box and the buttons
            StCombo {
                visible: view.browser.subvolumes.length > 0
                small: true
                Layout.preferredWidth: 220
                model: [{ label: "default volume", volume: "", snapshot: "" }].concat(view.browser.subvolumes.map(function(s) {
                    return { label: s.kind + " " + s.name + (s.parent.length ? " of " + s.parent : ""), volume: s.kind === "snapshot" ? s.parent : s.name, snapshot: s.kind === "snapshot" ? s.name : "" }
                }))
                textRole: "label"
                focusPolicy: Qt.TabFocus
                onActivated: (i) => { var m = model[i]; view.browser.openSubvolume(m.volume, m.snapshot); view.focusList() }
            }
            Item { Layout.fillWidth: true }
            // TabFocus on the view's controls: a click must not take the keyboard from the list.
            StButton { text: "Mount instead"; small: true; focusPolicy: Qt.TabFocus; enabled: view.browser.canMount && !JobRunner.running; onClicked: view.browser.mount() }
            StButton { text: "Copy all…"; small: true; focusPolicy: Qt.TabFocus; onClicked: view.copyEntry(-1) }
            StButton { text: "Copy out…"; variant: "primary"; small: true; focusPolicy: Qt.TabFocus; enabled: view.browser.hasSelection; onClicked: view.copyEntry(view.browser.selectedIndex) }
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
                    keyNavigationEnabled: false   // or a click on the tree's empty space takes the keyboard, and the tree has no keys
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
                            Text { Layout.preferredWidth: 10; text: treeRow.hasChildren ? (treeRow.expanded ? "▾" : "▸") : ""; font.pixelSize: Theme.fontMicro; color: Theme.textDim }
                            Icon { name: "folder"; size: 14; color: Theme.accentStep(400) }
                            Text { Layout.fillWidth: true; text: treeRow.model.name; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: treeRow.model.isCurrent ? Theme.accentStep(100) : Theme.textBright; elide: Text.ElideRight }
                        }
                        MouseArea {
                            id: treeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: { view.browser.goTo(treeRow.model.path); if (treeRow.hasChildren && !treeRow.expanded) treeRow.treeView.expand(treeRow.row); view.focusList() }
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
                        readonly property int rowHeight: 26
                        // Directory and row last scrolled into view: a preview arriving later must not
                        // pull the list back to a row the user has scrolled away from.
                        property string revealed: ""
                        function revealSelection() {
                            var row = view.browser.selectedIndex
                            var key = view.browser.pathText + "\n" + row
                            if (key === revealed) return
                            revealed = key
                            if (row >= 0) positionViewAtIndex(row, ListView.Contain)
                        }
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: view.browser.entries
                        activeFocusOnTab: true
                        // The selection lives in the model: the view's own arrow keys would move its currentIndex.
                        keyNavigationEnabled: false
                        Keys.onPressed: (event) => { event.accepted = view.keyPressed(event.key, event.modifiers, event.isAutoRepeat) }
                        TapHandler { onTapped: view.focusList() }   // the space below the last row
                        Connections {
                            target: view.browser
                            function onSelectionChanged() { entries.revealSelection() }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: erow
                            required property var model
                            required property int index
                            width: entries.width
                            height: entries.rowHeight
                            // The selected row is tinted with the accent while the list has the keyboard and
                            // grey while it is elsewhere. The light scheme's darkest accent step is nearly the
                            // surface itself, so it takes a stronger one.
                            color: !model.selected ? (erowMouse.containsMouse ? Theme.sunken : "transparent")
                                 : view.listFocused ? (Theme.dark ? Theme.accentStep(900) : Theme.accentStep(700))
                                 : (Theme.dark ? Theme.neutralStep(800) : Theme.neutralStep(700))
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                visible: erow.model.selected
                                color: view.listFocused ? Theme.accent : Theme.edge3
                            }
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
                                    // Only a link shares the column with its target, and its name stops at 360 for it.
                                    Text { text: erow.model.name; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; elide: Text.ElideRight; Layout.maximumWidth: erow.model.linkTarget.length > 0 ? 360 : Number.POSITIVE_INFINITY; Layout.minimumWidth: 0; Layout.fillWidth: erow.model.linkTarget.length === 0 }
                                    Text { visible: erow.model.linkTarget.length > 0; Layout.fillWidth: true; Layout.minimumWidth: 0; text: erow.model.linkTarget; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight }
                                }
                                Text { Layout.preferredWidth: 80; Layout.minimumWidth: 80; Layout.maximumWidth: 80; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight; text: erow.model.sizeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft }
                                Text { Layout.preferredWidth: 130; Layout.minimumWidth: 130; Layout.maximumWidth: 130; elide: Text.ElideRight; text: erow.model.mtimeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft }
                                Text { Layout.preferredWidth: 90; Layout.minimumWidth: 90; Layout.maximumWidth: 90; elide: Text.ElideRight; text: erow.model.modeText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
                            }
                            // Drag-out: past the drag threshold the entry is staged to a temp copy and
                            // handed to the OS as a file URL; dropping it in Finder / Explorer copies it.
                            Item {
                                id: dragProxy
                                Drag.dragType: Drag.Automatic
                                Drag.supportedActions: Qt.CopyAction
                                Drag.mimeData: ({})
                            }
                            MouseArea {
                                id: erowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                property real pressX: 0
                                property real pressY: 0
                                property bool dragging: false
                                onPressed: (mouse) => { pressX = mouse.x; pressY = mouse.y; dragging = false; view.focusList() }
                                onPositionChanged: (mouse) => {
                                    if (!pressed || dragging) return
                                    if (Math.abs(mouse.x - pressX) < 8 && Math.abs(mouse.y - pressY) < 8) return
                                    dragging = true
                                    var urls = view.browser.stageForDrag([erow.index])
                                    if (urls.length === 0) return
                                    dragProxy.Drag.mimeData = { "text/uri-list": urls.join("\r\n") }
                                    dragProxy.Drag.startDrag(Qt.CopyAction)
                                }
                                onClicked: if (!dragging) view.browser.select(erow.index)
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
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space3
                        Kicker { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "Preview · " + (view.paneMode === "hex" ? "hex" : view.browser.previewKind) }
                        StCheck {
                            Layout.fillWidth: false
                            Layout.minimumWidth: implicitWidth
                            text: "auto"
                            spacing: 6
                            font.pixelSize: Theme.fontTiny
                            focusPolicy: Qt.TabFocus
                            checked: Settings.previewFiles
                            onToggled: { Settings.previewFiles = checked; view.focusList() }
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
                            ToolTip.text: "Read and show each file as it is selected"
                        }
                    }
                    Text { Layout.fillWidth: true; text: view.browser.hasSelection ? view.browser.selectedName : "nothing selected"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH3; font.weight: Theme.headingWeight; color: Theme.text; elide: Text.ElideMiddle }
                    Text { Layout.fillWidth: true; text: view.browser.selectedInfo; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WrapAnywhere }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSm
                    color: Theme.sunken
                    clip: true
                    Item {
                        anchors.fill: parent
                        anchors.margins: 10
                        // The previous file's preview stays up, dimmed, for the moment the next one takes.
                        opacity: view.browser.previewLoading && view.browser.previewKind !== "loading" ? 0.4 : 1
                        Behavior on opacity { NumberAnimation { duration: Theme.quick } }
                        Flickable {
                            anchors.fill: parent
                            contentWidth: previewText.implicitWidth
                            contentHeight: previewText.implicitHeight
                            clip: true
                            visible: view.paneMode === "hex" || view.paneMode === "text"
                            ScrollBar.vertical: ScrollBar {}
                            ScrollBar.horizontal: ScrollBar {}
                            TextEdit {
                                id: previewText
                                readOnly: true
                                selectByMouse: true
                                text: view.paneMode === "hex" ? view.browser.previewHex : view.paneMode === "text" ? view.browser.previewText : ""
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontTiny
                                color: Theme.neutralStep(300)
                                selectionColor: Theme.accentStep(700)
                            }
                        }
                        PreviewSurface {
                            anchors.fill: parent
                            visible: view.paneMode === "image"
                            image: view.browser.previewImage
                            checkerColor: Theme.neutralStep(800)
                            checkerAltColor: Theme.neutralStep(700)
                        }
                        Text {
                            anchors.fill: parent
                            visible: view.paneMode === "message"
                            text: view.browser.previewKind === "loading" && view.browser.previewNote.length === 0 ? "reading…" : view.browser.previewNote
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.Wrap
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: view.browser.previewKind === "unreadable" ? Theme.warning : Theme.textMuted
                        }
                        ColumnLayout {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            visible: view.paneMode === "off"
                            spacing: Theme.space4
                            Text {
                                Layout.fillWidth: true
                                text: "Previews are off: nothing is read from a file until you ask."
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textMuted
                            }
                            StButton {
                                Layout.alignment: Qt.AlignHCenter
                                text: "Preview this file"
                                small: true
                                focusPolicy: Qt.TabFocus
                                enabled: view.browser.previewKind === "off" && !view.browser.previewLoading
                                onClicked: { view.browser.previewOnce(); view.focusList() }
                            }
                        }
                    }
                }
                // Under a picture, text or hex: what it is, or why it is not shown as what it claims to be.
                Text { visible: view.browser.previewNote.length > 0 && view.paneMode !== "message"; opacity: view.browser.previewLoading ? 0.4 : 1; text: view.browser.previewNote; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: view.browser.previewKind === "unreadable" ? Theme.warning : Theme.textMuted; Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                KeyValueGrid {
                    Layout.fillWidth: true
                    fontSize: Theme.fontTiny
                    rows: [{ key: "mtime", value: view.browser.mtimeText, mono: false }, { key: "crtime", value: view.browser.crtimeText, mono: false }, { key: "SHA-256", value: view.browser.hashText, mono: true }]
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    StButton { text: "Hash"; small: true; focusPolicy: Qt.TabFocus; enabled: view.browser.hasSelection && !JobRunner.running; onClicked: { view.browser.hash(); view.focusList() } }
                    StButton { text: !view.browser.showHex ? "Hex" : view.browser.previewKind === "image" ? "Image" : "Text"; small: true; focusPolicy: Qt.TabFocus; enabled: view.browser.hasSelection; onClicked: { view.browser.showHex = !view.browser.showHex; view.focusList() } }
                    Item { Layout.fillWidth: true }
                    StButton { text: "Copy out…"; variant: "primary"; small: true; focusPolicy: Qt.TabFocus; enabled: view.browser.hasSelection; onClicked: view.copyEntry(view.browser.selectedIndex) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Text { text: view.browser.statusText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
            Text { text: view.browser.previewEnabled ? "lazy: a directory is read when it opens, a file when it is selected" : "lazy: a directory is read when it opens, a file only when you ask"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight; text: view.browser.busy ? JobRunner.title + "…" : "↑ ↓ move · Enter opens a folder · Backspace goes up · Copy out writes to a folder of your choice"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
        }
    }
}
