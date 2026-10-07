// SPDX-License-Identifier: MIT
// Two columns: muted keys, mono values that wrap.
import QtQuick
import QtQuick.Layouts
import DrStein

GridLayout {
    id: grid
    property var rows: []        // [{key, value, mono}]
    property int fontSize: Theme.fontSmall
    columns: 2
    columnSpacing: 14
    rowSpacing: 4
    Repeater {
        model: grid.rows
        delegate: Item {
            required property var modelData
            required property int index
            Layout.row: index
            Layout.column: 0
            Layout.alignment: Qt.AlignTop
            implicitWidth: k.implicitWidth
            implicitHeight: k.implicitHeight
            Text { id: k; text: modelData.key; font.family: Theme.fontFamily; font.pixelSize: grid.fontSize; color: Theme.textMuted }
        }
    }
    Repeater {
        model: grid.rows
        delegate: Text {
            required property var modelData
            required property int index
            Layout.row: index
            Layout.column: 1
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: modelData.value
            font.family: modelData.mono === false ? Theme.fontFamily : Theme.monoFamily
            font.pixelSize: modelData.mono === false ? grid.fontSize : grid.fontSize - 0.5
            color: modelData.key === "Warning" ? Theme.warning : modelData.key === "Error" ? Theme.danger : Theme.text
            wrapMode: Text.WrapAnywhere
        }
    }
}
