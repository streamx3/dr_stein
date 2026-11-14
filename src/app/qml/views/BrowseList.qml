// SPDX-License-Identifier: MIT
// Browse's list layout: the current directory as a table. The name takes the room; when the
// pane narrows the mode goes first, then the modified time.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Rectangle {
    id: pane
    required property FileBrowser browser
    required property int rowHeight
    // False in the column layout: the list then holds no model, so it lays out no rows.
    required property bool shown
    readonly property bool focused: entries.activeFocus
    readonly property bool showMode: width >= 600
    readonly property bool showModified: width >= 460
    signal focusRequested()

    function takeFocus() { entries.forceActiveFocus() }
    function pageRows() { return Math.max(1, Math.floor(entries.height / pane.rowHeight) - 1) }

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
            columns: [{ label: "Name" }, { label: "Size", width: 72, align: "right" }]
                     .concat(pane.showModified ? [{ label: "Modified", width: 124 }] : [])
                     .concat(pane.showMode ? [{ label: "Mode", width: 84 }] : [])
        }
        ListView {
            id: entries
            // Directory and row last scrolled into view: a preview arriving later must not
            // pull the list back to a row the user has scrolled away from.
            property string revealed: ""
            function revealSelection() {
                var row = pane.browser.selectedIndex
                var key = pane.browser.pathText + "\n" + row
                if (key === revealed) return
                revealed = key
                if (row >= 0) positionViewAtIndex(row, ListView.Contain)
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: pane.shown ? pane.browser.entries : null
            onModelChanged: { revealed = ""; Qt.callLater(entries.revealSelection) }
            activeFocusOnTab: true
            // The selection lives in the model: the view's own arrow keys would move its currentIndex.
            keyNavigationEnabled: false
            TapHandler { onTapped: pane.focusRequested() }   // the space below the last row
            Connections {
                target: pane.browser
                enabled: pane.shown
                function onSelectionChanged() { entries.revealSelection() }
            }
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: erow
                required property var model
                required property int index
                width: entries.width
                height: pane.rowHeight
                // The selected row is tinted with the accent while the list has the keyboard and
                // grey while it is elsewhere. The light scheme's darkest accent step is nearly the
                // surface itself, so it takes a stronger one.
                color: !model.selected ? (erowMouse.containsMouse ? Theme.sunken : "transparent")
                     : pane.focused ? (Theme.dark ? Theme.accentStep(900) : Theme.accentStep(700))
                     : (Theme.dark ? Theme.neutralStep(800) : Theme.neutralStep(700))
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    visible: erow.model.selected
                    color: pane.focused ? Theme.accent : Theme.edge3
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
                        Text {
                            id: erowName
                            text: erow.model.name
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: Theme.text
                            elide: Text.ElideMiddle   // keeps the extension
                            Layout.maximumWidth: erow.model.linkTarget.length > 0 ? 360 : Number.POSITIVE_INFINITY
                            Layout.minimumWidth: 0
                            Layout.fillWidth: erow.model.linkTarget.length === 0
                            ToolTip.visible: erowMouse.containsMouse && !erowMouse.pressed && erowName.truncated
                            ToolTip.delay: 600
                            ToolTip.text: erow.model.name
                        }
                        Text { visible: erow.model.linkTarget.length > 0; Layout.fillWidth: true; Layout.minimumWidth: 0; text: erow.model.linkTarget; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight }
                    }
                    Text { Layout.preferredWidth: 72; Layout.minimumWidth: 72; Layout.maximumWidth: 72; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight; text: erow.model.sizeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft }
                    Text { visible: pane.showModified; Layout.preferredWidth: 124; Layout.minimumWidth: 124; Layout.maximumWidth: 124; elide: Text.ElideRight; text: erow.model.mtimeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft }
                    Text { visible: pane.showMode; Layout.preferredWidth: 84; Layout.minimumWidth: 84; Layout.maximumWidth: 84; elide: Text.ElideRight; text: erow.model.modeText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted }
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
                    onPressed: (mouse) => { pressX = mouse.x; pressY = mouse.y; dragging = false; pane.focusRequested() }
                    onPositionChanged: (mouse) => {
                        if (!pressed || dragging) return
                        if (Math.abs(mouse.x - pressX) < 8 && Math.abs(mouse.y - pressY) < 8) return
                        dragging = true
                        var urls = pane.browser.stageForDrag([erow.index])
                        if (urls.length === 0) return
                        dragProxy.Drag.mimeData = { "text/uri-list": urls.join("\r\n") }
                        dragProxy.Drag.startDrag(Qt.CopyAction)
                    }
                    onClicked: if (!dragging) pane.browser.select(erow.index)
                    onDoubleClicked: pane.browser.activate(erow.index)
                }
            }
        }
    }
}
