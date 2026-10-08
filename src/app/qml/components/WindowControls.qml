// SPDX-License-Identifier: MIT
// Minimise / maximise / close for platforms whose caption buttons we draw
// ourselves. macOS keeps its traffic lights. Windows: 36 px chips at the top
// of the bar with our stroke icons. Linux: VS Code's caption buttons, 46 px
// wide and the full height of the bar, with its Codicon glyphs
// (icons/codicons/), its 10 % hover overlay and its red close.
pragma ComponentBehavior: Bound
import QtQuick
import DrStein

Row {
    id: controls
    required property Window window
    spacing: 0
    readonly property bool vscode: Qt.platform.os === "linux"
    property int barHeight: 48               // the Linux buttons span it; set by TopBar
    readonly property bool maximized: window.visibility === Window.Maximized || window.visibility === Window.FullScreen

    component Chip: Item {
        id: chip
        property string icon_: "dot"        // Icons.js name (Windows)
        property string codicon: ""         // file in icons/codicons (Linux)
        property bool danger: false
        signal clicked()
        width: 46
        height: controls.vscode ? controls.barHeight : 36
        readonly property bool hot: chipMouse.containsMouse
        // Linux glyphs are pure white / black (the 1 px Codicon strokes are too light in a grey);
        // Windows keeps the row's icon colour.
        readonly property color glyph: hot && danger ? "white" : controls.vscode ? (Theme.dark ? "white" : "black") : Theme.textSoft
        Rectangle {
            anchors.fill: parent
            color: !chip.hot ? "transparent"
                 : chip.danger ? (controls.vscode ? Qt.rgba(232 / 255, 17 / 255, 35 / 255, 0.9) : Theme.danger)
                 : controls.vscode ? (Theme.dark ? Qt.rgba(1, 1, 1, 0.1) : Qt.rgba(0, 0, 0, 0.1)) : Theme.surface
            Behavior on color { ColorAnimation { duration: Theme.quick } }
        }
        Icon {
            visible: !controls.vscode
            anchors.centerIn: parent
            name: chip.icon_
            size: 12
            strokeWidth: 1.4
            color: chip.glyph
        }
        Image {
            visible: controls.vscode
            anchors.centerIn: parent
            width: 16
            height: 16
            sourceSize.width: 32
            sourceSize.height: 32
            smooth: true
            source: controls.vscode ? "image://codicon/" + chip.codicon + "/" + chip.glyph.toString().substring(1) : ""
        }
        MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; onClicked: chip.clicked() }
    }

    Chip { icon_: "minimize"; codicon: "chrome-minimize"; onClicked: controls.window.showMinimized() }
    Chip {
        icon_: controls.maximized ? "restore" : "maximize"
        codicon: controls.maximized ? "chrome-restore" : "chrome-maximize"
        onClicked: controls.maximized ? controls.window.showNormal() : controls.window.showMaximized()
    }
    Chip { icon_: "close"; codicon: "chrome-close"; danger: true; onClicked: controls.window.close() }
}
