// SPDX-License-Identifier: MIT
// The small uppercase label above a card title or a table column.
import QtQuick
import DrStein

Text {
    property color tint: Theme.textMuted
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontMicro
    font.letterSpacing: 1
    font.capitalization: Font.AllUppercase
    color: tint
    elide: Text.ElideRight
}
