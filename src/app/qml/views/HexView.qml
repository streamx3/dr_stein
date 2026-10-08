// SPDX-License-Identifier: MIT
// Struct picker · hex pane · parsed pane; edits preview on an overlay until Write.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import DrStein

Item {
    id: view
    property StructInspector inspector: StructInspector {}
    signal confirmWrite()

    FileDialog {
        id: saveDialog
        title: "Save structure as a piece"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Stein piece (*.sparse)"]
        defaultSuffix: "sparse"
        onAccepted: { var e = view.inspector.saveHeader(selectedFile); if (e.message !== undefined) Workspace.error(e) }
    }
    FileDialog {
        id: restoreDialog
        title: "Restore structure from a piece"
        nameFilters: ["Stein piece (*.sparse)", "All files (*)"]
        onAccepted: { var e = view.inspector.restoreFromFile(selectedFile); if (e.message !== undefined) Workspace.error(e) }
    }

    EmptyState {
        anchors.fill: parent
        visible: !view.inspector.available
        icon_: "layers"
        title: "No structure here"
        message: view.inspector.unavailableReason
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space4
        spacing: Theme.space3
        visible: view.inspector.available
        enabled: !Workspace.uiLocked
        opacity: enabled ? 1 : 0.6

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3
            // The structure switcher wraps into more rows rather than crowding out the position text.
            StSegmented {
                id: structs
                wrap: true
                Layout.fillWidth: true
                Layout.minimumWidth: widestOption
                Layout.maximumWidth: oneRowWidth
                model: view.inspector.structs.map(function(s, i) { return { label: s.label, value: i } })
                currentValue: view.inspector.currentStruct
                onPicked: (v) => view.inspector.currentStruct = v
            }
            Text { Layout.minimumWidth: implicitWidth; Layout.maximumWidth: implicitWidth; text: view.inspector.where; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted; elide: Text.ElideRight }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: view.inspector.dirtyCount > 0 ? "pending: " + view.inspector.dirtyRanges : ""; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.accentStep(300); elide: Text.ElideRight }
            StButton { text: "Save header\u2026"; small: true; onClicked: saveDialog.open() }
            StButton { text: "Restore from file\u2026"; small: true; onClicked: restoreDialog.open() }
            StButton { text: "Fix " + view.inspector.fixableChecksums + (view.inspector.fixableChecksums === 1 ? " checksum" : " checksums"); small: true; visible: view.inspector.fixableChecksums > 0; onClicked: view.inspector.fixChecksums() }
            StButton { text: "Revert"; variant: "ghost"; small: true; enabled: view.inspector.dirtyCount > 0; onClicked: view.inspector.revert() }
            StButton {
                text: "Write " + view.inspector.dirtyCount + (view.inspector.dirtyCount === 1 ? " byte\u2026" : " bytes\u2026")
                variant: "primary"; small: true
                enabled: view.inspector.dirtyCount > 0 && view.inspector.canWrite && !JobRunner.running
                onClicked: view.confirmWrite()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space4

            // Hex pane.
            Rectangle {
                Layout.preferredWidth: Math.round((view.width - 2 * Theme.space6 - Theme.space4) * 0.56)
                Layout.fillHeight: true
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.edge1
                clip: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10
                            Kicker { Layout.preferredWidth: 76; text: "offset"; font.letterSpacing: 0.6 }
                            Row {
                                Layout.fillWidth: true
                                spacing: 3
                                Repeater {
                                    model: 16
                                    delegate: Kicker {
                                        required property int index
                                        width: (parent.width - 15 * 3) / 16
                                        horizontalAlignment: Text.AlignHCenter
                                        text: ("0" + index.toString(16)).slice(-2)
                                        font.letterSpacing: 0
                                    }
                                }
                            }
                            Kicker { Layout.preferredWidth: 136; text: "ascii"; font.letterSpacing: 0.6 }
                        }
                        FadeDivider { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
                    }
                    ListView {
                        id: hexList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: view.inspector.rows
                        topMargin: 4
                        bottomMargin: 8
                        leftMargin: 12
                        rightMargin: 12
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item {
                            id: hexRow
                            required property var model
                            width: hexList.width - 24
                            height: 20
                            RowLayout {
                                anchors.fill: parent
                                spacing: 10
                                Text { Layout.preferredWidth: 76; text: hexRow.model.offsetText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textDim }
                                Row {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Repeater {
                                        model: 16
                                        delegate: Rectangle {
                                            id: cell
                                            required property int index
                                            readonly property real byteOffset: Number(hexRow.model.offset) + index
                                            readonly property bool inSel: view.inspector.selectedField >= 0 && byteOffset >= Number(view.inspector.selectionStart) && byteOffset < Number(view.inspector.selectionEnd)
                                            readonly property bool dirty: hexRow.model.dirty[index] === true
                                            readonly property string hex: hexRow.model.hex[index]
                                            width: parent ? (parent.width - 15 * 3) / 16 : 0   // parent is gone while the row is torn down
                                            height: 18
                                            radius: 3
                                            color: inSel ? Theme.accentStep(800) : "transparent"
                                            border.width: cellMouse.containsMouse ? 1 : 0
                                            border.color: Theme.accent
                                            Text {
                                                anchors.centerIn: parent
                                                text: cell.hex
                                                font.family: Theme.monoFamily
                                                font.pixelSize: Theme.fontSmall
                                                color: cell.dirty ? Theme.accentStep(300) : cell.inSel ? Theme.accentStep(100) : cell.hex === "00" ? Theme.textDim : Theme.textBright
                                            }
                                            MouseArea { id: cellMouse; anchors.fill: parent; hoverEnabled: true; onClicked: view.inspector.selectByte(cell.byteOffset) }
                                        }
                                    }
                                }
                                Text { Layout.preferredWidth: 136; text: hexRow.model.ascii; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; font.letterSpacing: 0.5; color: Theme.textSoft }
                            }
                        }
                    }
                }
            }

            // Parsed pane.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.edge1
                clip: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            Kicker { text: "parsed · " + view.inspector.label; font.letterSpacing: 0.6; Layout.fillWidth: true }
                            Kicker { text: view.inspector.validity; font.letterSpacing: 0.6; font.capitalization: Font.MixedCase; tint: view.inspector.validity.indexOf("flagged") >= 0 || view.inspector.validity.indexOf("no longer") >= 0 ? Theme.warning : Theme.textMuted }
                        }
                        FadeDivider { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
                    }
                    ListView {
                        id: fieldList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: view.inspector.fields
                        topMargin: 6
                        bottomMargin: 8
                        leftMargin: 12
                        rightMargin: 12
                        currentIndex: view.inspector.selectedField
                        ScrollBar.vertical: ScrollBar {}
                        header: Text { text: "{"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted; height: 20 }
                        footer: Text { text: "}"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted; height: 20 }
                        delegate: Rectangle {
                            id: frow
                            required property var model
                            required property int index
                            property bool editing: false
                            width: fieldList.width - 24
                            height: Math.max(20, editRow.implicitHeight + 2)
                            radius: 3
                            color: model.selected ? Theme.accentStep(900) : (frowMouse.containsMouse ? Theme.sunken : "transparent")
                            RowLayout {
                                id: editRow
                                anchors.fill: parent
                                anchors.leftMargin: 8 + frow.model.depth * 16
                                anchors.rightMargin: 8
                                spacing: 12
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 6
                                    Text { text: "\"" + frow.model.name + "\""; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.accentStep(300); elide: Text.ElideMiddle; Layout.maximumWidth: 180 }
                                    Text { text: ":"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted }
                                    StTextField {
                                        id: editor
                                        visible: frow.editing
                                        Layout.preferredWidth: 220
                                        mono: true
                                        small: true
                                        text: frow.model.value
                                        onAccepted: { var e = view.inspector.editField(frow.index, text); if (e.message !== undefined) Workspace.error(e); else frow.editing = false }
                                        Keys.onEscapePressed: frow.editing = false
                                        onVisibleChanged: if (visible) { forceActiveFocus(); selectAll() }
                                    }
                                    Text {
                                        visible: !frow.editing
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: frow.model.display
                                        font.family: Theme.monoFamily
                                        font.pixelSize: Theme.fontSmall
                                        color: frow.model.dirty ? Theme.accentStep(300) : frow.model.isStruct ? Theme.textMuted : frow.model.validity === "error" ? Theme.danger : frow.model.validity === "warning" ? Theme.warning : Theme.neutralStep(100)
                                        elide: Text.ElideRight
                                    }
                                    Text { visible: !frow.editing && frow.model.pretty.length > 0 && frow.model.pretty !== frow.model.value; text: frow.model.pretty; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 200 }
                                }
                                Row {
                                    spacing: 10
                                    Text { text: frow.model.typeName; font.family: Theme.monoFamily; font.pixelSize: Theme.fontMicro; color: Theme.textDim }
                                    Text { text: "@" + frow.model.offsetText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontMicro; color: Theme.textDim }
                                    Text { text: frow.model.size + " B"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontMicro; color: Theme.textDim }
                                    Text {
                                        text: frow.model.dirty ? "edited" : frow.model.validity === "ok" ? "ok" : frow.model.validity
                                        font.family: Theme.monoFamily; font.pixelSize: Theme.fontMicro
                                        color: frow.model.dirty ? Theme.accentStep(300) : frow.model.validity === "error" ? Theme.danger : frow.model.validity === "warning" ? Theme.warning : Theme.textDim
                                    }
                                }
                            }
                            MouseArea {
                                id: frowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                z: -1
                                onClicked: view.inspector.selectField(frow.index)
                                onDoubleClicked: if (frow.model.editable) { view.inspector.selectField(frow.index); frow.editing = true }
                                ToolTip.visible: containsMouse && (frow.model.doc.length > 0 || frow.model.message.length > 0)
                                ToolTip.text: (frow.model.message.length ? frow.model.message + "\n" : "") + frow.model.doc
                                ToolTip.delay: 700
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: view.inspector.selectedFieldLine; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; elide: Text.ElideRight }
            Text { text: "double-click a value to edit · click bytes to select the field · edits preview on an overlay until Write"; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.textDim }
        }
    }
}
