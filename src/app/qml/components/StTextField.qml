// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DrStein

TextField {
    id: field
    property bool mono: false
    property bool small: false

    font.family: mono ? Theme.monoFamily : Theme.fontFamily
    font.pixelSize: small || mono ? Theme.fontSmall : Theme.fontSize
    color: Theme.text
    placeholderTextColor: Theme.textDim
    selectionColor: Theme.accentStep(700)
    selectedTextColor: Theme.accentStep(100)
    leftPadding: 9
    rightPadding: 9
    topPadding: small ? 4 : 6
    bottomPadding: small ? 4 : 6
    implicitHeight: small ? Theme.controlHeightSmall : Theme.controlHeight + 2
    implicitWidth: 160          // never the text's width: fields stretch, they do not push
    Layout.minimumWidth: 0
    opacity: enabled ? 1 : Theme.disabledOpacity

    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.bg
        border.width: 1
        border.color: field.activeFocus ? Theme.accent : Theme.edge1
        Behavior on border.color { ColorAnimation { duration: Theme.quick } }
    }
}
