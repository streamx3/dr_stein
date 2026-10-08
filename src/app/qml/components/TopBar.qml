// SPDX-License-Identifier: MIT
// Brand · view tabs · elevation tag · refresh · theme toggle · settings.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Item {
    id: bar
    property Window window: null
    property bool customChrome: false
    signal openSettings()
    signal openMounts()
    property int leftInsetOverride: -1
    readonly property int leftInset: !customChrome ? 0 : leftInsetOverride >= 0 ? leftInsetOverride : WindowChrome.leftInset
    readonly property bool ownButtons: customChrome && !WindowChrome.nativeButtons

    // The whole bar moves the window; children (tabs, buttons) take their clicks first.
    DragHandler {
        enabled: bar.customChrome && bar.window !== null
        target: null
        onActiveChanged: if (active) bar.window.startSystemMove()
    }
    TapHandler {
        enabled: bar.customChrome && bar.window !== null && Qt.platform.os !== "osx"   // AppKit zooms on its own
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: if (bar.window.visibility === Window.Maximized) bar.window.showNormal(); else bar.window.showMaximized()
    }
    readonly property var views: [
        { id: "topology", label: "Topology" }, { id: "hex", label: "Hex" }, { id: "browse", label: "Browse" },
        { id: "image", label: "Image" }, { id: "profiles", label: "Profiles" }, { id: "tools", label: "Tools" }, { id: "partitions", label: "Partitions" }
    ]
    implicitHeight: 48

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6 + bar.leftInset
        anchors.rightMargin: bar.ownButtons ? 0 : Theme.space6
        spacing: Theme.space4

        Row {
            Layout.minimumWidth: Math.max(0, Settings.sidebarWidth - Theme.space6 - Theme.space4 - bar.leftInset)
            spacing: 8
            Icon { name: "disk"; size: 18; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: "Dr Stein"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH2
                font.weight: Theme.headingWeight
                color: Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            spacing: 2
            enabled: !Workspace.uiLocked
            opacity: enabled ? 1 : 0.45
            Repeater {
                model: bar.views
                delegate: Item {
                    id: tab
                    required property var modelData
                    readonly property bool current: Workspace.view === modelData.id
                    width: label.implicitWidth + 24
                    height: 48
                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: tab.modelData.label
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        color: tab.current ? Theme.accent : (tabMouse.containsMouse ? Theme.accentStep(300) : Theme.neutralStep(300))
                    }
                    Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 2; color: Theme.accent; visible: tab.current }
                    MouseArea { id: tabMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Workspace.view = tab.modelData.id }
                }
            }
        }
        Item { Layout.fillWidth: true }
        StTag {
            text: Workspace.elevated ? "Elevated · " + (Qt.platform.os === "windows" ? "Administrator" : "root") : "Not elevated · images only"
            variant: Workspace.elevated ? "neutral" : "outline"
            dot: Workspace.elevated
        }
        // With hot-plug the list keeps itself current; the button stays as the manual re-read of the open disk.
        StButton { text: "Refresh"; small: true; icon_: "refresh"; enabled: !Workspace.uiLocked; onClicked: Workspace.refresh(); ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: Workspace.hotplug ? "Devices update on their own; this re-reads the open disk" : "Re-list devices and re-read the open disk" }
        IconButton { icon_: "mount"; tip: MountsModel.count + " mount(s) made by Dr Stein"; visible: MountsModel.count > 0; onClicked: bar.openMounts() }
        IconButton { icon_: Theme.dark ? "sun" : "moon"; tip: Theme.dark ? "Switch to light" : "Switch to dark"; onClicked: Settings.colorScheme = Theme.dark ? "light" : "dark" }
        IconButton { icon_: "gear"; tip: "Settings"; onClicked: bar.openSettings() }
        // Linux: the buttons span the bar like VS Code's; Windows: chips at the top like the system's.
        WindowControls {
            visible: bar.ownButtons
            window: bar.window
            barHeight: bar.height
            Layout.leftMargin: Theme.space3
            Layout.alignment: vscode ? Qt.AlignVCenter : Qt.AlignTop
        }
    }
    FadeDivider { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
}
