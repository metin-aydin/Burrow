import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

BDialog {
    id: root
    dialogWidth: 520
    title: qsTr("Ayarlar")
    property string pwError
    property string touchError

    onOpened: { cur.text = ""; next.text = ""; again.text = ""; pwError = ""; touchError = ""; touch.checked = App.touchIdEnabled }

    SectionTitle { text: qsTr("Güvenlik") }
    BSwitch {
        id: touch
        Layout.fillWidth: true
        enabled: App.touchIdAvailable
        title: qsTr("Touch ID İle Aç")
        description: App.touchIdAvailable ? qsTr("Kasa, ana şifre yerine parmak iziyle açılabilir. Ana şifre her zaman geçerli kalır.")
                                          : qsTr("Bu Mac'te Touch ID yok.")
        checked: App.touchIdEnabled
        onToggled: { root.touchError = App.setTouchIdEnabled(checked); checked = App.touchIdEnabled }
    }
    Text { visible: root.touchError !== ""; text: root.touchError; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }

    SectionTitle { text: qsTr("Ana Şifreyi Değiştir") }
    BField { id: cur; Layout.fillWidth: true; label: qsTr("Mevcut Şifre"); password: true }
    RowLayout {
        spacing: 12
        BField { id: next; Layout.fillWidth: true; label: qsTr("Yeni Şifre"); password: true }
        BField { id: again; Layout.fillWidth: true; label: qsTr("Yeni Şifre (Tekrar)"); password: true }
    }
    RowLayout {
        Text { Layout.fillWidth: true; text: root.pwError; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap }
        BButton {
            text: qsTr("Şifreyi Değiştir")
            onClicked: {
                root.pwError = App.changePassword(cur.text, next.text, again.text)
                if (!root.pwError) { cur.text = ""; next.text = ""; again.text = "" }
            }
        }
    }

    SectionTitle { text: qsTr("Hakkında") }
    Text { text: App.versionInfo; color: Theme.textSecondary; font.pixelSize: 12 }

    footer: [
        BButton { iconName: "lock"; text: qsTr("Şimdi Kilitle"); onClicked: { root.close(); App.lock() } },
        Item { Layout.fillWidth: true },
        BButton { variant: "primary"; text: qsTr("Bitti"); onClicked: root.close() }
    ]
}
