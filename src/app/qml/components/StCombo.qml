// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DrStein

ComboBox {
    id: control
    property bool small: false
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
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        leftPadding: 0
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
        contentItem: Text {
            text: control.textRole ? item.model[control.textRole] : item.model.modelData !== undefined ? item.model.modelData : item.model.display
            font: control.font
            color: item.highlighted ? Theme.accentStep(100) : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            leftPadding: 9
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
