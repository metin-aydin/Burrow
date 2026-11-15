import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

BDialog {
    id: root
    dialogWidth: 520
    title: qsTr("Settings")
    property string pwError
    property string touchError

    onOpened: { cur.text = ""; next.text = ""; again.text = ""; pwError = ""; touchError = ""; touch.checked = App.touchIdEnabled }

    SectionTitle { text: qsTr("Language"); first: true }
    BCombo {
        Layout.fillWidth: true
        options: [{ text: qsTr("System Language"), value: "system" },
                  { text: "English", value: "en" },
                  { text: "Türkçe", value: "tr" }]
        value: App.language
        onValueChanged: if (value !== App.language) App.language = value
    }

    SectionTitle { text: qsTr("Appearance") }
    function demoCombo() { themeCombo.openPopup() }   // development (BURROW_DEMO_OPEN=combo)
    BCombo {
        id: themeCombo
        Layout.fillWidth: true
        options: [{ text: qsTr("Match System"), value: "system" },
                  { text: qsTr("Light"), value: "light" },
                  { text: qsTr("Dark"), value: "dark" }]
        value: App.theme
        onValueChanged: if (value !== App.theme) App.theme = value
    }

    SectionTitle { text: qsTr("Security") }
    BSwitch {
        id: touch
        Layout.fillWidth: true
        enabled: App.touchIdAvailable
        title: qsTr("Unlock With Touch ID")
        description: App.touchIdAvailable ? qsTr("Unlock the vault with your fingerprint instead of the master password. The master password always keeps working.")
                                          : qsTr("This Mac has no Touch ID.")
        checked: App.touchIdEnabled
        onToggled: { root.touchError = App.setTouchIdEnabled(checked); checked = App.touchIdEnabled }
    }
    Text { visible: root.touchError !== ""; text: root.touchError; color: Theme.danger; font.pixelSize: Theme.fontLabel; wrapMode: Text.WordWrap; Layout.fillWidth: true }

    BCombo {
        Layout.fillWidth: true
        label: qsTr("Auto-Lock")
        options: [{ text: qsTr("After 5 minutes of inactivity"), value: 5 },
                  { text: qsTr("After 15 minutes of inactivity"), value: 15 },
                  { text: qsTr("After 30 minutes of inactivity"), value: 30 },
                  { text: qsTr("After 1 hour of inactivity"), value: 60 },
                  { text: qsTr("Never"), value: 0 }]
        value: App.autoLockMinutes
        onValueChanged: if (value !== App.autoLockMinutes) App.autoLockMinutes = value
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
        text: qsTr("Burrow keeps running in the menu bar when you close its window; it locks itself after this much inactivity, but never while a remote desktop is open.")
    }

    SectionTitle { text: qsTr("Change Master Password") }
    BField { id: cur; Layout.fillWidth: true; label: qsTr("Current Password"); password: true }
    RowLayout {
        spacing: 12
        BField { id: next; Layout.fillWidth: true; label: qsTr("New Password"); password: true }
        BField { id: again; Layout.fillWidth: true; label: qsTr("New Password (Again)"); password: true }
    }
    RowLayout {
        Text { Layout.fillWidth: true; text: root.pwError; color: Theme.danger; font.pixelSize: Theme.fontLabel; wrapMode: Text.WordWrap }
        BButton {
            text: qsTr("Change Password")
            onClicked: {
                root.pwError = App.changePassword(cur.text, next.text, again.text)
                if (!root.pwError) { cur.text = ""; next.text = ""; again.text = "" }
            }
        }
    }

    SectionTitle { text: qsTr("Updates") }
    BSwitch {
        Layout.fillWidth: true
        enabled: App.updater.supported
        title: qsTr("Check For Updates Automatically")
        description: App.updater.supported ? qsTr("Checks GitHub every 6 hours. Nothing is downloaded without asking.")
                                           : qsTr("Automatic updates aren't available on this platform yet.")
        checked: App.updater.autoCheck
        onToggled: App.updater.autoCheck = checked
    }
    RowLayout {
        visible: App.updater.supported
        spacing: 8
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontLabel
            color: App.updater.state === "error" ? Theme.danger : Theme.textSecondary
            text: {
                const u = App.updater
                switch (u.state) {
                case "checking": return qsTr("Checking for updates…")
                case "upToDate": return qsTr("Burrow is up to date.")
                case "available": return qsTr("Burrow %1 is available.").arg(u.latestVersion)
                case "downloading": return qsTr("Downloading %1… %2%").arg(u.latestVersion).arg(Math.round(u.progress * 100))
                case "ready": return qsTr("Burrow %1 is ready to install. Burrow restarts to finish.").arg(u.latestVersion)
                case "installing": return qsTr("Installing…")
                case "error": return u.error
                }
                return ""
            }
        }
        BButton {
            visible: App.updater.state === "idle" || App.updater.state === "upToDate" || App.updater.state === "error"
            text: qsTr("Check Now")
            onClicked: App.updater.checkNow()
        }
        BButton {
            visible: App.updater.state === "available"
            variant: "primary"
            text: qsTr("Download Update")
            onClicked: App.updater.download()
        }
        BButton {
            visible: App.updater.state === "ready"
            variant: "primary"
            text: qsTr("Restart And Update")
            onClicked: App.updater.install()
        }
    }

    SectionTitle { text: qsTr("About") }
    Text { text: App.versionInfo; color: Theme.textSecondary; font.pixelSize: Theme.fontLabel }

    footer: [
        Item { Layout.fillWidth: true },
        BButton { variant: "primary"; text: qsTr("Done"); onClicked: root.close() }
    ]
}
