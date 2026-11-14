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

    SectionTitle { text: qsTr("About") }
    Text { text: App.versionInfo; color: Theme.textSecondary; font.pixelSize: 12 }

    footer: [
        BButton { iconName: "lock"; text: qsTr("Lock Now"); onClicked: { root.close(); App.lock() } },
        Item { Layout.fillWidth: true },
        BButton { variant: "primary"; text: qsTr("Done"); onClicked: root.close() }
    ]
}
