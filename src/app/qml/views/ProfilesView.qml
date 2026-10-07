// SPDX-License-Identifier: MIT
// One card per profile: disk, match, image, made, fits, three big buttons.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import DrStein

Item {
    id: view
    signal newProfile()
    signal confirmRestore(int row)
    signal askPassphrase(string what, var callback)

    function run(row, scenario, dryRun) {
        var card = ProfilesModel.get(row)
        if (card.encrypted) view.askPassphrase(scenario + " " + card.name, function(p) { ProfilesModel.run(row, scenario, dryRun, p) })
        else ProfilesModel.run(row, scenario, dryRun, "")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.bottomMargin: Theme.space6
        spacing: Theme.space4
        Text {
            Layout.maximumWidth: 640
            text: "One profile is one disk and one image. Three buttons, each dry-runnable; the verdict comes from the library, not the screen."
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted; wrapMode: Text.WordWrap
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: cardsColumn.implicitHeight
            contentWidth: width
            clip: true
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: cardsColumn
                width: parent.width
                spacing: Theme.space4
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space4
                    enabled: !Workspace.uiLocked
                    opacity: enabled ? 1 : 0.6
                    Repeater {
                        model: ProfilesModel
                        delegate: StCard {
                            id: card
                            required property var model
                            required property int index
                            width: Math.max(300, (cardsColumn.width - Theme.space4 * 2) / 3)
                            gap: Theme.space3
                            opacity: model.diskPresent ? 1 : 0.75
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                RowLayout {
                                    Layout.fillWidth: true
                                    Kicker { Layout.fillWidth: true; text: card.model.kicker }
                                    IconButton { icon_: "trash"; iconSize: 12; implicitWidth: 20; implicitHeight: 20; tip: "Delete this profile (the image stays)"; onClicked: { var e = ProfilesModel.remove(card.index); if (e.message !== undefined) Workspace.error(e) } }
                                }
                                Text { Layout.fillWidth: true; text: card.model.name; font.family: Theme.fontFamily; font.pixelSize: Theme.fontH2; font.weight: Theme.headingWeight; color: Theme.text; elide: Text.ElideRight }
                                Text { Layout.fillWidth: true; visible: card.model.description.length > 0 && card.model.description !== card.model.kicker; text: card.model.description; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textSoft; wrapMode: Text.WordWrap }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: facts.implicitHeight + 20
                                radius: Theme.radiusSm
                                color: Theme.sunken
                                GridLayout {
                                    id: facts
                                    anchors.fill: parent; anchors.margins: 10
                                    columns: 2; columnSpacing: 12; rowSpacing: 5
                                    Text { text: "Disk"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted }
                                    HealthDot { level: card.model.diskPresent ? "ok" : "info"; text: card.model.diskText; Layout.fillWidth: true }
                                    Text { text: "Match"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted; Layout.alignment: Qt.AlignTop }
                                    Text { Layout.fillWidth: true; text: card.model.selectorText; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny; color: Theme.text; wrapMode: Text.WrapAnywhere }
                                    Text { text: "Image"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted }
                                    HealthDot { level: card.model.imagePresent ? "ok" : "info"; text: card.model.imageText; Layout.fillWidth: true }
                                    Text { text: "Made"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted }
                                    Text { Layout.fillWidth: true; text: card.model.madeText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; elide: Text.ElideRight }
                                    Text { text: "Fits"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.textMuted }
                                    Text { Layout.fillWidth: true; text: card.model.fitsText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall; color: Theme.text; elide: Text.ElideRight }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                StButton { Layout.fillWidth: true; text: "Back up"; subtext: "disk → image"; variant: "primary"; enabled: card.model.canBackup && !JobRunner.running; onClicked: view.run(card.index, "backup", false) }
                                StButton { Layout.fillWidth: true; text: "Restore"; subtext: "image → disk"; enabled: card.model.canRestore && !JobRunner.running; onClicked: view.confirmRestore(card.index) }
                                StButton { Layout.fillWidth: true; text: "Verify"; subtext: card.model.verifyLevelText; enabled: card.model.canVerify && !JobRunner.running; onClicked: view.run(card.index, "verify", false) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                StButton { text: "Dry run backup"; variant: "ghost"; small: true; enabled: card.model.canBackup && !JobRunner.running; onClicked: view.run(card.index, "backup", true) }
                                StButton { text: "Dry run restore"; variant: "ghost"; small: true; enabled: card.model.canRestore && !JobRunner.running; onClicked: view.run(card.index, "restore", true) }
                            }
                            Repeater { model: card.model.warnings; delegate: Text { required property string modelData; Layout.fillWidth: true; text: "⚠ " + modelData; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.warning; wrapMode: Text.WordWrap } }
                            Text { Layout.fillWidth: true; text: card.model.policyText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
                        }
                    }
                    StButton {
                        width: Math.max(300, (cardsColumn.width - Theme.space4 * 2) / 3)
                        height: 160
                        dashed: true
                        text: ProfilesModel.createHint
                        icon_: "plus"
                        enabled: ProfilesModel.canCreateFromCurrent
                        onClicked: view.newProfile()
                    }
                }
                JobCard {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 180
                    kind: "profile"
                    idleKicker: "Profiles in " + ProfilesModel.directory
                    idleText: ProfilesModel.count === 0 ? "No profiles yet. Select a disk and create one." : "Nothing running."
                }
            }
        }
    }
}
