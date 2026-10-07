// SPDX-License-Identifier: MIT
import QtQuick
import DrStein
import "../Icons.js" as Icons

Image {
    id: icon
    property string name: "dot"
    property color color: Theme.text
    property int size: 16
    property real strokeWidth: 1.6

    width: size
    height: size
    sourceSize.width: size * 2
    sourceSize.height: size * 2
    smooth: true
    fillMode: Image.PreserveAspectFit
    source: Icons.svg(name, color.toString(), strokeWidth)
}
