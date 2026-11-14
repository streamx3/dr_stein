// SPDX-License-Identifier: MIT
// Browse's column layout, as Finder shows a folder: one column per folder from the root to the
// current one, then the contents of the folder selected there (the peek). The columns share
// one width: dragging any divider sets it, a double-click on one fits the longest name shown.
// Columns that stay with their folder keep their rows and scroll position (FileBrowser).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Rectangle {
    id: pane
    required property FileBrowser browser
    required property int rowHeight
    readonly property bool focused: strip.activeFocus
    // Settings.browseColumnWidth, or the width a divider is being dragged to.
    property int columnWidth: Settings.browseColumnWidth
    readonly property int leastWidth: 140
    readonly property int mostWidth: 640
    // What a column is shown at: never wider than the strip, so a width chosen in a wider window
    // still shows one whole column.
    readonly property int shownWidth: Math.max(pane.leastWidth, Math.min(pane.columnWidth, Math.floor(strip.width) - 1))
    readonly property int step: pane.shownWidth + 1   // a column and its divider
    // The right end of the view where reveal() or a divider drag last put it. The content reaches
    // at least that far, so the strip does not shift when the peek comes or goes.
    property real extent: 0
    property int revealedColumn: -1
    property string revealedSelection: ""
    // Between a column's rows and its divider: the divider's grip reaches into it.
    readonly property int gutter: 4
    // A row's width besides its name: margins, icon, chevron, the gaps between them, the gutter.
    readonly property int rowChrome: 10 + 14 + 6 + 6 + 10 + 8 + gutter
    signal focusRequested()

    function takeFocus() { strip.forceActiveFocus() }
    function pageRows() { return Math.max(1, Math.floor((strip.height - 8) / pane.rowHeight) - 1) }
    // A drag sets the width as it goes and stores it when it ends.
    function setColumnWidth(width, store) {
        pane.columnWidth = Math.max(pane.leastWidth, Math.min(pane.mostWidth, Math.floor(strip.width) - 1, Math.round(width)))
        if (!store) return
        Settings.browseColumnWidth = pane.columnWidth
        pane.columnWidth = Qt.binding(() => Settings.browseColumnWidth)
    }
    // Wide enough for the longest name in any column of the strip, scrolled into view or not, but
    // not so wide that the current column and the peek no longer stand side by side.
    function fitColumns() {
        var longest = 0
        var columns = pane.browser.columns
        for (var c = 0; c < columns.count; ++c) {
            var rows = columns.get(c).entries
            for (var r = 0; r < rows.rowCount(); ++r) longest = Math.max(longest, nameMetrics.advanceWidth(rows.get(r).name))
        }
        var pair = Math.floor((strip.width - 1) / 2)
        pane.setColumnWidth(Math.min(Math.ceil(longest) + pane.rowChrome + 2, Math.max(pair, pane.shownWidth)), true)
        pane.reveal(true)
    }
    function currentColumn() {
        var columns = pane.browser.columns
        for (var i = columns.count - 1; i >= 0; --i) {
            if (columns.get(i).current) return i
        }
        return -1
    }
    // Whole columns from the left edge, with the current column and the place of the peek after
    // it at the right end; when the two do not fit side by side, the current column whole at the
    // left. The peek's place counts whether a folder is selected or not, so moving between files
    // and folders does not shift the strip. Unless forced or the current column changed, a view
    // that shows both already stays: a divider drag or a scroll by hand is not undone.
    function reveal(force) {
        var cur = pane.currentColumn()
        if (cur < 0) {
            pane.revealedColumn = -1
            return
        }
        var view = strip.width
        var left = cur * pane.step
        var right = left + 2 * pane.step - 1
        var x = strip.contentX
        var shown = left >= x && left + pane.shownWidth <= x + view && (right <= x + view || right - left > view)
        if (!force && cur === pane.revealedColumn && shown) return
        pane.revealedColumn = cur
        var whole = Math.max(1, Math.floor((view + 1) / pane.step))
        x = Math.max(0, Math.min(cur, cur + 2 - whole)) * pane.step
        pane.extent = x + view
        strip.contentX = x
    }
    function revealAgain() { pane.reveal(true) }
    function revealIfNeeded() { pane.reveal(false) }

    // A preview that arrives later is a selection change too: only a step of the selection may
    // bring the strip back from where it was scrolled.
    Connections {
        target: pane.browser
        function onSelectionChanged() {
            var key = pane.browser.pathText + "\n" + pane.browser.selectedIndex
            if (key === pane.revealedSelection) return
            pane.revealedSelection = key
            Qt.callLater(pane.revealIfNeeded)
        }
    }

    radius: Theme.radiusMd
    color: Theme.surface
    border.width: 1
    border.color: Theme.edge1
    clip: true

    FontMetrics { id: nameMetrics; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall }

    Flickable {
        id: strip
        anchors.fill: parent
        anchors.margins: 1
        contentWidth: Math.max(columnRepeater.count * pane.step, pane.extent)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        activeFocusOnTab: true
        onWidthChanged: Qt.callLater(pane.revealAgain)
        ScrollBar.horizontal: ScrollBar {}

        Row {
            id: columnRow
            height: strip.height
            Repeater {
                id: columnRepeater
                model: pane.browser.columns
                onCountChanged: Qt.callLater(pane.revealIfNeeded)
                delegate: Item {
                    id: column
                    required property int index
                    required property bool current
                    required property int selectedRow
                    required property EntriesModel entries
                    required property int entryCount
                    required property string error
                    width: pane.step
                    height: columnRow.height
                    z: -column.index   // the divider's grip reaches over the next column's edge

                    onSelectedRowChanged: rows.revealSelection()
                    Component.onCompleted: rows.revealSelection()

                    ListView {
                        id: rows
                        function revealSelection() {
                            if (column.selectedRow < 0) return
                            rows.forceLayout()
                            rows.positionViewAtIndex(column.selectedRow, ListView.Contain)
                        }
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 4
                        anchors.bottomMargin: 4
                        width: pane.shownWidth - pane.gutter
                        clip: true
                        model: column.entries
                        activeFocusOnTab: false
                        // The selection lives in the model; the strip takes the keys.
                        keyNavigationEnabled: false
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        // The space below the last row: in an ancestor it goes back there, in the peek it enters.
                        TapHandler { onTapped: { pane.focusRequested(); pane.browser.columnClicked(column.index, -1) } }
                        delegate: Rectangle {
                            id: crow
                            required property int index
                            required property string name
                            required property string kind
                            required property bool selected
                            // The current column's selection while the strip has the keyboard; every
                            // other selected row (an ancestor's, or the strip without the keyboard) is grey.
                            readonly property bool lit: crow.selected && column.current && pane.focused
                            width: rows.width
                            height: pane.rowHeight
                            color: !crow.selected ? (crowMouse.containsMouse ? Theme.sunken : "transparent")
                                 : crow.lit ? (Theme.dark ? Theme.accentStep(900) : Theme.accentStep(700))
                                 : (Theme.dark ? Theme.neutralStep(800) : Theme.neutralStep(700))
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                visible: crow.selected
                                color: crow.lit ? Theme.accent : Theme.edge3
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 6
                                Icon { name: crow.kind === "dir" ? "folder" : crow.kind === "link" ? "link" : "file"; size: 14; color: crow.kind === "dir" ? Theme.accentStep(400) : Theme.textMuted }
                                Text {
                                    id: crowName
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: crow.name
                                    elide: Text.ElideMiddle   // keeps the extension
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    color: Theme.text
                                    ToolTip.visible: crowMouse.containsMouse && !crowMouse.pressed && crowName.truncated
                                    ToolTip.delay: 600
                                    ToolTip.text: crow.name
                                }
                                Icon { visible: crow.kind === "dir"; name: "chevronRight"; size: 10; color: crow.lit ? Theme.text : Theme.textMuted }
                            }
                            // Drag-out from any column: the entry is staged and handed to the OS as a
                            // file URL; neither the selection nor the current folder changes.
                            Item {
                                id: dragProxy
                                Drag.dragType: Drag.Automatic
                                Drag.supportedActions: Qt.CopyAction
                                Drag.mimeData: ({})
                            }
                            MouseArea {
                                id: crowMouse
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
                                    var urls = pane.browser.stageColumnForDrag(column.index, crow.index)
                                    if (urls.length === 0) return
                                    dragProxy.Drag.mimeData = { "text/uri-list": urls.join("\r\n") }
                                    dragProxy.Drag.startDrag(Qt.CopyAction)
                                }
                                onClicked: if (!dragging) pane.browser.columnClicked(column.index, crow.index)
                                onDoubleClicked: pane.browser.columnActivated(column.index, crow.index)
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: rows
                        width: rows.width - 24
                        visible: column.entryCount === 0 && column.error.length === 0
                        text: "empty folder"
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                    Text {
                        anchors.left: rows.left
                        anchors.right: rows.right
                        anchors.top: rows.top
                        anchors.margins: 12
                        visible: column.error.length > 0
                        text: column.error
                        wrapMode: Text.Wrap
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: Theme.warning
                    }

                    Rectangle {
                        x: pane.shownWidth
                        width: 1
                        height: parent.height
                        color: grip.pressed || grip.containsMouse ? Theme.accent : Theme.edge1
                    }
                    // The divider's grip. Every column takes the dragged width, and the dragged
                    // divider follows the pointer one to one: its column's left edge stays put, the
                    // strip scrolls by what the columns to its left gained. The pointer is measured
                    // in the pane, which does not move with the columns.
                    MouseArea {
                        id: grip
                        property real pressX: 0
                        property int startWidth: 0
                        property real startX: 0
                        property bool dragged: false
                        x: pane.shownWidth - pane.gutter
                        width: 2 * pane.gutter + 1
                        height: parent.height
                        hoverEnabled: true
                        preventStealing: true   // a drag here is not a flick of the strip
                        cursorShape: Qt.SplitHCursor
                        onPressed: (mouse) => {
                            grip.pressX = grip.mapToItem(pane, mouse.x, 0).x
                            grip.startWidth = pane.shownWidth
                            grip.startX = strip.contentX
                            grip.dragged = false
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed) return
                            var moved = grip.mapToItem(pane, mouse.x, 0).x - grip.pressX
                            var k = column.index
                            grip.dragged = true
                            pane.setColumnWidth(grip.startWidth + moved, false)
                            var x = grip.startX + k * (pane.columnWidth - grip.startWidth)
                            if (x < 0) {
                                // Scrolled to the start: the columns to the left shrink in view, and
                                // the dragged divider moves by its share of the narrowing.
                                pane.setColumnWidth(grip.startWidth + (moved - grip.startX) / (k + 1), false)
                                x = 0
                            }
                            pane.extent = x + strip.width
                            strip.contentX = x
                        }
                        // A press without a drag stores nothing: the width shown may be a stored
                        // wider one cut to a narrow window.
                        onReleased: if (grip.dragged) pane.setColumnWidth(pane.columnWidth, true)
                        onCanceled: if (grip.dragged) pane.setColumnWidth(pane.columnWidth, true)
                        onDoubleClicked: pane.fitColumns()
                    }
                }
            }
        }
    }
}
