// SPDX-License-Identifier: MIT
// Errors from the workspace and the job runner, bottom-right, auto-hiding.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import DrStein

Item {
    id: host
    property var queue: []
    function show(err) {
        if (!err || err.message === undefined) return
        queue = queue.concat([err])
        if (queue.length > 4) queue = queue.slice(queue.length - 4)
        hideTimer.restart()
    }
    function dismiss(i) {
        var q = queue.slice(); q.splice(i, 1); queue = q
    }
    Connections { target: Workspace; function onError(e) { host.show(e) } }
    Connections { target: JobRunner; function onError(e) { host.show(e) } }
    Timer { id: hideTimer; interval: 9000; onTriggered: host.queue = [] }

    anchors.fill: parent
    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space6
        spacing: Theme.space2
        Repeater {
            model: host.queue
            delegate: Rectangle {
                id: toast
                required property var modelData
                required property int index
                readonly property color tint: modelData.severity === "error" ? Theme.danger : modelData.severity === "warning" ? Theme.warning : Theme.accent
                width: 380
                implicitHeight: body.implicitHeight + 24
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.edge2
                Rectangle { width: 3; height: parent.height - 16; y: 8; x: 0; radius: 2; color: toast.tint }
                ColumnLayout {
                    id: body
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 12
                    anchors.leftMargin: 16
                    spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: toast.modelData.title; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.weight: Font.Medium; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                        IconButton { icon_: "close"; iconSize: 12; implicitWidth: 20; implicitHeight: 20; onClicked: host.dismiss(toast.index) }
                    }
                    Text { Layout.fillWidth: true; text: toast.modelData.message; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textBright; wrapMode: Text.WordWrap }
                    Text { Layout.fillWidth: true; visible: toast.modelData.hint !== undefined && toast.modelData.hint.length > 0; text: toast.modelData.hint !== undefined ? toast.modelData.hint : ""; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
                }
            }
        }
    }
}
