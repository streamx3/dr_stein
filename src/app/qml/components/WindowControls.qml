// SPDX-License-Identifier: MIT
// Minimise / maximise / close for platforms whose caption buttons we draw
// ourselves (Windows, Linux). macOS keeps its traffic lights.
import QtQuick
import DrStein

Row {
    id: controls
    required property Window window
    spacing: 0
    readonly property bool maximized: window.visibility === Window.Maximized || window.visibility === Window.FullScreen

    component Chip: Item {
        id: chip
        property string icon_: "dot"
        property bool danger: false
        signal clicked()
        width: 46
        height: 36
        Rectangle {
            anchors.fill: parent
            color: chipMouse.containsMouse ? (chip.danger ? Theme.danger : Theme.surface) : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.quick } }
        }
        Icon {
            anchors.centerIn: parent
            name: chip.icon_
            size: 12
            strokeWidth: 1.4
            color: chipMouse.containsMouse && chip.danger ? "white" : Theme.textSoft
        }
        MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; onClicked: chip.clicked() }
    }

    Chip { icon_: "minimize"; onClicked: controls.window.showMinimized() }
    Chip { icon_: controls.maximized ? "restore" : "maximize"; onClicked: controls.maximized ? controls.window.showNormal() : controls.window.showMaximized() }
    Chip { icon_: "close"; danger: true; onClicked: controls.window.close() }
}
