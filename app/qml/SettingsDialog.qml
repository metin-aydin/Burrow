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

    SectionTitle { text: qsTr("Language") }
    BCombo {
        Layout.fillWidth: true
        options: [{ text: qsTr("System Language"), value: "system" },
                  { text: "English", value: "en" },
                  { text: "Türkçe", value: "tr" }]
        value: App.language
        onValueChanged: if (value !== App.language) App.language = value
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
    Text { visible: root.touchError !== ""; text: root.touchError; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }

    SectionTitle { text: qsTr("Change Master Password") }
    BField { id: cur; Layout.fillWidth: true; label: qsTr("Current Password"); password: true }
    RowLayout {
        spacing: 12
        BField { id: next; Layout.fillWidth: true; label: qsTr("New Password"); password: true }
        BField { id: again; Layout.fillWidth: true; label: qsTr("New Password (Again)"); password: true }
    }
    RowLayout {
        Text { Layout.fillWidth: true; text: root.pwError; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap }
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
            font.pixelSize: 12
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
    Text { text: App.versionInfo; color: Theme.textSecondary; font.pixelSize: 12 }

    footer: [
        BButton { iconName: "lock"; text: qsTr("Lock Now"); onClicked: { root.close(); App.lock() } },
        Item { Layout.fillWidth: true },
        BButton { variant: "primary"; text: qsTr("Done"); onClicked: root.close() }
    ]
}
