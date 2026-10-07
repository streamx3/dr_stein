// SPDX-License-Identifier: MIT
// A modal at the top elevation over a dimmed backdrop (Nocturne .dialog).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Popup {
    id: dialog
    property string title: ""
    property string body: ""
    default property alias content: inner.data
    property alias actions: actionRow.data
    property int dialogWidth: 460

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: dialogWidth
    padding: Theme.space6
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.55 : 0.3) }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.edge3
    }
    contentItem: ColumnLayout {
        id: column
        spacing: Theme.space4
        Text { Layout.fillWidth: true; Layout.minimumWidth: 0; visible: dialog.title.length > 0; text: dialog.title; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH2; font.weight: Theme.headingWeight; color: Theme.text; wrapMode: Text.WordWrap }
        Text { Layout.fillWidth: true; Layout.minimumWidth: 0; visible: dialog.body.length > 0; text: dialog.body; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.textSoft; wrapMode: Text.WordWrap }
        ColumnLayout { id: inner; Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: Theme.space4 }
        Row { id: actionRow; Layout.alignment: Qt.AlignRight; Layout.topMargin: Theme.space2; spacing: 6; layoutDirection: Qt.RightToLeft }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.quick } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.quick } }
}
