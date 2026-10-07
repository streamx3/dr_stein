// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import DrStein

StDialog {
    id: dialog
    title: "Settings"
    dialogWidth: 480
    FolderDialog { id: profilesDirDialog; title: "Profiles folder"; onAccepted: Settings.profilesDir = selectedFolder.toString().replace("file://", "") }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "Palette"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        StSegmented { model: [{ label: "Teal", value: "teal" }, { label: "Blurple", value: "blurple" }, { label: "Blue", value: "blue" }, { label: "Grey", value: "grey" }]; currentValue: Settings.palette; onPicked: (v) => Settings.palette = v }
    }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "Appearance"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        StSegmented { model: [{ label: "Dark", value: "dark" }, { label: "Light", value: "light" }, { label: "Follow system", value: "system" }]; currentValue: Settings.colorScheme; onPicked: (v) => Settings.colorScheme = v }
    }
    StCheck { text: "Draw the title bar in the app"; sublabel: "The tabs sit in the title bar, like an editor. Applies at the next launch."; checked: Settings.customTitleBar; onToggled: Settings.customTitleBar = checked }
    StCheck { visible: Settings.expertMode; text: "Allow using the app while a read or write runs"; sublabel: "Dangerous: switching sources or starting edits during a write can corrupt the target. Off, everything but Cancel is blocked until the job ends."; checked: Settings.interactDuringJobs; onToggled: Settings.interactDuringJobs = checked }
    StCheck { text: "Expert mode"; sublabel: "Shows the partition table's metadata regions in the topology and lets the Hex view write to them."; checked: Settings.expertMode; onToggled: Settings.expertMode = checked }
    ColumnLayout { Layout.fillWidth: true; spacing: 4
        Kicker { text: "Profiles folder"; font.capitalization: Font.MixedCase; font.letterSpacing: 0; font.pixelSize: Theme.fontSmall }
        RowLayout { Layout.fillWidth: true; spacing: 6
            StTextField { Layout.fillWidth: true; mono: true; text: Settings.profilesDir; onEditingFinished: Settings.profilesDir = text }
            StButton { text: "Choose…"; small: true; onClicked: profilesDirDialog.open() }
        }
    }
    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "Dr Stein " + Workspace.version + " · libstein on " + Workspace.platformName + " · " + (Workspace.elevated ? "elevated" : "not elevated: raw disks need sudo / Run as administrator"); font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny; color: Theme.textMuted; wrapMode: Text.WordWrap }
    actions: [ StButton { text: "Close"; variant: "primary"; onClicked: dialog.close() } ]
}
