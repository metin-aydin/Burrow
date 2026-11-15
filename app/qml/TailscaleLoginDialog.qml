import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

BDialog {
    id: root
    dialogWidth: 460
    property string url
    title: qsTr("Sign In To Tailscale")

    function show(name, loginUrl) {
        url = loginUrl
        subtitle = qsTr("%1 needs to join your tailnet. Sign in once in your browser; Burrow keeps the node key in its encrypted vault.").arg(name)
        open()
    }

    Text {
        Layout.fillWidth: true
        text: root.url
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
        wrapMode: Text.WrapAnywhere
    }
    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Later"); onClicked: root.close() },
        BButton { variant: "primary"; text: qsTr("Open Sign-In Page"); onClicked: { Qt.openUrlExternally(root.url); root.close() } }
    ]
}
