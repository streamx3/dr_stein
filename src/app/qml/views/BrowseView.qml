// SPDX-License-Identifier: MIT
// Over a read-only Reader, in one of two layouts (Settings.browseLayout): columns, as Finder
// shows a folder, or a directory tree beside the entries as a table. Either way the preview
// card is on the right; the panes are split by handles whose widths are remembered. The
// columns or the list hold the keyboard; the preview follows the selection.
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
    readonly property bool columnsLayout: Settings.browseLayout === "columns"
    readonly property bool listFocused: columnsPane.focused || listPane.focused
    readonly property int rowHeight: 26

    // Pane widths. Each side pane is the width last dragged to, unless the window is too narrow
    // for it and the middle's least width: then the preview gives way first, then the tree.
    readonly property int middleLeast: 320
    readonly property real paneRoom: panes.width - (view.columnsLayout ? 1 : 2) * Theme.space4
    // The least widths (tree 150, middle 240, preview 220), scaled down together when the room is
    // less than they add up to, as with a wide sidebar in a narrow window: the panes never run
    // past the window's edge.
    readonly property real leastScale: Math.max(0, Math.min(1, view.paneRoom / ((view.columnsLayout ? 0 : 150) + 240 + 220)))
    readonly property int treeLeast: Math.floor(150 * view.leastScale)
    readonly property int middleMin: Math.floor(240 * view.leastScale)
    readonly property int previewLeast: Math.floor(220 * view.leastScale)
    readonly property int previewPaneWidth: {
        var tree = view.columnsLayout ? 0 : Settings.browseTreeWidth
        var excess = tree + Settings.browsePreviewWidth + view.middleLeast - view.paneRoom
        return Math.max(view.previewLeast, Settings.browsePreviewWidth - Math.max(0, excess))
    }
    readonly property int treePaneWidth: {
        var excess = Settings.browseTreeWidth + Settings.browsePreviewWidth + view.middleLeast - view.paneRoom
        var previewGives = Math.max(0, Math.min(excess, Settings.browsePreviewWidth - view.previewLeast))
        return Math.max(view.treeLeast, Settings.browseTreeWidth - Math.max(0, excess - previewGives))
    }
    // A drag stores only the pane it moved: the other may be squeezed for now, not chosen.
    property int treeBefore: 0
    property int previewBefore: 0
    function panesResizeStarted() {
        view.treeBefore = Math.round(treePane.width)
        view.previewBefore = Math.round(previewPane.width)
    }
    function panesResized() {
        var tree = Math.round(treePane.width)
        var preview = Math.round(previewPane.width)
        if (treePane.visible && tree !== view.treeBefore) Settings.browseTreeWidth = tree
        if (preview !== view.previewBefore) Settings.browsePreviewWidth = preview
        // The drag replaced the bindings with numbers.
        treePane.SplitView.preferredWidth = Qt.binding(() => view.treePaneWidth)
        previewPane.SplitView.preferredWidth = Qt.binding(() => view.previewPaneWidth)
    }
    // What the preview pane shows: "off" (the explanation and the button), "message" (a line
    // of words), "image", "hex" or "text".
    readonly property string paneMode: {
        var kind = view.browser.previewKind
        if (kind === "off" || (kind === "none" && !view.browser.previewEnabled)) return "off"
        if (view.browser.showHex && view.browser.previewHex.length > 0) return "hex"
        return kind === "image" || kind === "hex" || kind === "text" ? kind : "message"
    }

    // The keyboard. The Keys handler of the columns or the list calls keyPressed(); a
    // DRSTEIN_SCRIPT, which cannot press keys, calls the same functions.
    function pageRows() { return view.columnsLayout ? columnsPane.pageRows() : listPane.pageRows() }
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
        // A held key does not walk through the directories. In columns, as in Finder, the folder
        // entered has its first entry selected; the list selects nothing there.
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Right:
            if (autoRepeat) return true
            if (view.columnsLayout) view.browser.enterSelectedAndSelectFirst()
            else view.browser.activateSelected()
            return true
        case Qt.Key_Backspace: case Qt.Key_Left:
            if (!autoRepeat) view.browser.up()
            return true
        }
        return false
    }
    // The columns or the list take the keyboard, but never from a dialog: then they take it
    // when the dialog lets go.
    property bool focusWanted: false
    function focusList() {
        for (var item = view.Window.activeFocusItem; item; item = item.parent) {
            if (item === view.Overlay.overlay) { view.focusWanted = true; return }
        }
        view.focusWanted = false
        if (view.columnsLayout) columnsPane.takeFocus()
        else listPane.takeFocus()
    }
    Connections {
        target: view.Window.window
        enabled: view.focusWanted
        function onActiveFocusItemChanged() { if (view.current) view.focusList(); else view.focusWanted = false }
    }
    onCurrentChanged: if (current) focusList()
    onColumnsLayoutChanged: if (current) focusList()
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
            // A path too long for the row gives way at its start: the deepest crumbs stay in view.
            Rectangle {
                id: crumbBox
                implicitHeight: 26
                Layout.fillWidth: true
                Layout.preferredWidth: Math.ceil(crumbs.implicitWidth) + 20
                Layout.maximumWidth: Math.ceil(crumbs.implicitWidth) + 20
                Layout.minimumWidth: Math.min(Math.ceil(crumbs.implicitWidth) + 20, 120)
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.edge1
                clip: true
                Rectangle {
                    z: 1
                    visible: crumbs.x < 9.5
                    x: 1
                    y: 1
                    width: 28
                    height: parent.height - 2
                    radius: Theme.radiusMd
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.4; color: Theme.surface }
                        GradientStop { position: 1; color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0) }
                    }
                }
                Row {
                    id: crumbs
                    x: Math.min(10, crumbBox.width - 10 - crumbs.implicitWidth)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text { text: view.browser.title; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.accentStep(300) }
                    Text { text: view.browser.fsName; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textDim }
                    Repeater {
                        model: view.browser.breadcrumbs
                        delegate: Row {
                            id: crumb
                            required property var modelData
                            spacing: 4
                            // The root crumb is the "/" itself; a separator goes only between names.
                            Text { text: "/"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textDim; visible: crumb.modelData.index > 1 }
                            Text {
                                text: crumb.modelData.index === 0 ? "/" : crumb.modelData.name
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

        SplitView {
            id: panes
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            // The gap between the panes is the handle; a short bar shows where to grab it.
            handle: Item {
                id: paneHandle
                implicitWidth: Theme.space4
                Rectangle {
                    anchors.centerIn: parent
                    width: 2
                    height: Math.min(40, parent.height)
                    radius: 1
                    visible: paneHandle.SplitHandle.hovered || paneHandle.SplitHandle.pressed
                    color: paneHandle.SplitHandle.pressed ? Theme.accent : Theme.edge3
                }
            }
            onResizingChanged: if (resizing) view.panesResizeStarted(); else view.panesResized()

            // Directory tree: the list layout's only.
            Rectangle {
                id: treePane
                visible: !view.columnsLayout
                SplitView.preferredWidth: view.treePaneWidth
                SplitView.minimumWidth: view.treeLeast
                SplitView.maximumWidth: 420
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

            // Entries: the column strip or the table.
            Item {
                SplitView.fillWidth: true
                // What the side panes' widths leave it; a drag stops there rather than jump on release.
                SplitView.minimumWidth: Math.max(view.middleMin, Math.min(view.middleLeast, view.paneRoom - view.previewLeast - (view.columnsLayout ? 0 : view.treeLeast)))
                BrowseColumns {
                    id: columnsPane
                    anchors.fill: parent
                    visible: view.columnsLayout
                    browser: view.browser
                    rowHeight: view.rowHeight
                    onFocusRequested: view.focusList()
                    Keys.onPressed: (event) => { event.accepted = view.keyPressed(event.key, event.modifiers, event.isAutoRepeat) }
                }
                BrowseList {
                    id: listPane
                    anchors.fill: parent
                    visible: !view.columnsLayout
                    shown: !view.columnsLayout
                    browser: view.browser
                    rowHeight: view.rowHeight
                    onFocusRequested: view.focusList()
                    Keys.onPressed: (event) => { event.accepted = view.keyPressed(event.key, event.modifiers, event.isAutoRepeat) }
                }
            }

            // Preview.
            StCard {
                id: previewPane
                SplitView.preferredWidth: view.previewPaneWidth
                SplitView.minimumWidth: view.previewLeast
                SplitView.maximumWidth: 560
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

        // Laid out by hand: in a narrow window the middle text gives way first, then the keys
        // elide at their end. Nothing here may widen the view.
        Item {
            id: bottomRow
            readonly property int gap: 16
            Layout.fillWidth: true
            implicitHeight: statusLine.implicitHeight
            Text { id: statusLine; text: view.browser.statusText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
            Text {
                x: statusLine.implicitWidth + bottomRow.gap
                width: Math.max(0, keyHint.x - bottomRow.gap - x)
                visible: width >= 40
                elide: Text.ElideRight
                text: view.browser.previewEnabled ? "lazy: a directory is read when it opens, a file when it is selected" : "lazy: a directory is read when it opens, a file only when you ask"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: Theme.textMuted
            }
            Text {
                id: keyHint
                anchors.right: parent.right
                width: Math.min(implicitWidth, Math.max(0, bottomRow.width - statusLine.implicitWidth - bottomRow.gap))
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                text: view.browser.busy ? JobRunner.title + "…"
                    : view.columnsLayout ? "← → folders · ↑ ↓ move · Enter opens · Copy out writes to a folder of your choice"
                    : "↑ ↓ move · Enter opens a folder · Backspace goes up · Copy out writes to a folder of your choice"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: Theme.textMuted
            }
        }
    }
}
