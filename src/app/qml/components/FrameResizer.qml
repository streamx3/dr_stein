// SPDX-License-Identifier: MIT
// Resize bands along the edges of a frameless window; the compositor does the
// resize through startSystemResize (xdg-shell on Wayland, _NET_WM_MOVERESIZE on X11).
import QtQuick
import DrStein

Item {
    id: resizer
    required property Window window
    property int band: 6
    anchors.fill: parent
    readonly property bool active: window.visibility === Window.Windowed

    component Edge: Item {
        property int edges: 0
        property int cursor: Qt.ArrowCursor
        visible: resizer.active
        DragHandler {
            target: null
            grabPermissions: PointerHandler.TakeOverForbidden
            onActiveChanged: if (active) resizer.window.startSystemResize(parent.edges)
        }
        HoverHandler { cursorShape: parent.cursor }
    }
    Edge { edges: Qt.LeftEdge; cursor: Qt.SizeHorCursor; x: 0; y: resizer.band; width: resizer.band; height: parent.height - 2 * resizer.band }
    Edge { edges: Qt.RightEdge; cursor: Qt.SizeHorCursor; x: parent.width - resizer.band; y: resizer.band; width: resizer.band; height: parent.height - 2 * resizer.band }
    Edge { edges: Qt.TopEdge; cursor: Qt.SizeVerCursor; x: resizer.band; y: 0; width: parent.width - 2 * resizer.band; height: resizer.band }
    Edge { edges: Qt.BottomEdge; cursor: Qt.SizeVerCursor; x: resizer.band; y: parent.height - resizer.band; width: parent.width - 2 * resizer.band; height: resizer.band }
    Edge { edges: Qt.TopEdge | Qt.LeftEdge; cursor: Qt.SizeFDiagCursor; x: 0; y: 0; width: resizer.band; height: resizer.band }
    Edge { edges: Qt.TopEdge | Qt.RightEdge; cursor: Qt.SizeBDiagCursor; x: parent.width - resizer.band; y: 0; width: resizer.band; height: resizer.band }
    Edge { edges: Qt.BottomEdge | Qt.LeftEdge; cursor: Qt.SizeBDiagCursor; x: 0; y: parent.height - resizer.band; width: resizer.band; height: resizer.band }
    Edge { edges: Qt.BottomEdge | Qt.RightEdge; cursor: Qt.SizeFDiagCursor; x: parent.width - resizer.band; y: parent.height - resizer.band; width: resizer.band; height: resizer.band }
}
