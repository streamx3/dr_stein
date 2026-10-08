// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import DrStein

StDialog {
    id: dialog
    title: "Mounts made by Dr Stein"
    body: MountsModel.count === 0 ? "Nothing mounted." : "Read-only exports of filesystems inside images, LUKS containers or LVM volumes. They end when unmounted here or when Dr Stein quits."
    dialogWidth: 560
    onOpened: MountsModel.reload()
    Repeater {
        model: MountsModel
        delegate: Rectangle {
            id: mountRow
            required property var model
            required property int index
            Layout.fillWidth: true
            implicitHeight: 44
            radius: Theme.radiusSm
            color: Theme.sunken
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
                Icon { name: "mount"; size: 14; color: mountRow.model.running ? Theme.ok : Theme.warning }
                ColumnLayout { Layout.fillWidth: true; spacing: 1
                    Text { Layout.fillWidth: true; text: mountRow.model.mountpoint; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; elide: Text.ElideMiddle }
                    Text { Layout.fillWidth: true; text: mountRow.model.what + (mountRow.model.error.length ? " · " + mountRow.model.error : ""); font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight }
                }
                StButton { text: "Unmount"; small: true; onClicked: MountsModel.unmount(mountRow.index) }
            }
        }
    }
    actions: [
        StButton { text: "Close"; variant: "primary"; onClicked: dialog.close() },
        StButton { text: "Unmount all"; visible: MountsModel.count > 0; onClicked: MountsModel.unmountAll() }
    ]
}
