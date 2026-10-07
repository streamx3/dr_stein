// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DrStein

ComboBox {
    id: control
    property bool small: false
    // When set, each row starts with this role in a mono font, padded by the model to one
    // width, so a list of devices reads as a table: "/dev/disk4   UDisk · 8.1 GB".
    property string monoRole: ""
    property string closedText: ""   // overrides displayText when the combo is closed
    font.family: Theme.fontFamily
    font.pixelSize: small ? Theme.fontSmall : Theme.fontSize
    implicitHeight: small ? Theme.controlHeightSmall : Theme.controlHeight + 2
    implicitWidth: 160
    Layout.minimumWidth: 0
    leftPadding: 9
    rightPadding: 28
    opacity: enabled ? 1 : Theme.disabledOpacity
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.bg
        border.width: 1
        border.color: control.activeFocus || control.popup.visible ? Theme.accent : Theme.edge1
    }
    contentItem: Row {
        spacing: 8
        Text {
            visible: control.monoRole.length > 0 && control.currentIndex >= 0
            text: visible ? control.model[control.currentIndex][control.monoRole] : ""
            font.family: Theme.monoFamily
            font.pixelSize: control.font.pixelSize
            color: Theme.accentStep(300)
            height: parent.height
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            text: control.closedText.length ? control.closedText : control.displayText
            font: control.font
            color: Theme.text
            height: parent.height
            width: parent.width - (control.monoRole.length > 0 && control.currentIndex >= 0 ? x : 0)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    indicator: Icon {
        x: control.width - width - 8
        y: (control.height - height) / 2
        name: "chevronDown"
        size: 12
        color: Theme.textMuted
    }
    delegate: ItemDelegate {
        id: item
        required property var model
        required property int index
        width: control.width
        height: 26
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        background: Rectangle { color: item.highlighted ? Theme.accentStep(900) : "transparent" }
        contentItem: Row {
            leftPadding: 9
            spacing: 8
            Text {
                visible: control.monoRole.length > 0
                text: visible ? item.model[control.monoRole] : ""
                font.family: Theme.monoFamily
                font.pixelSize: control.font.pixelSize
                color: item.highlighted ? Theme.accentStep(100) : Theme.accentStep(300)
                height: parent.height
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                text: control.textRole ? item.model[control.textRole] : item.model.modelData !== undefined ? item.model.modelData : item.model.display
                font: control.font
                color: item.highlighted ? Theme.accentStep(100) : Theme.text
                elide: Text.ElideRight
                height: parent.height
                width: parent.width - x - 9
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    popup: Popup {
        y: control.height + 2
        width: Math.max(control.width, 240)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.surface
            border.width: 1
            border.color: Theme.edge2
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
