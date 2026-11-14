import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Asks for the remote computer's username and password when they aren't saved.
BDialog {
    id: root
    dialogWidth: 420
    property string deviceId
    signal submitted(string deviceId, string username, string password)

    function ask(id, name, username) {
        deviceId = id
        title = qsTr("Sign In To %1").arg(name)
        subtitle = qsTr("Enter the account you use on this computer.")
        user.text = username || ""
        pw.text = ""
        open()
        if (user.text) pw.field.forceActiveFocus(); else user.field.forceActiveFocus()
    }
    function submit() {
        if (!user.text || !pw.text) return
        const p = pw.text
        pw.text = ""
        close()
        root.submitted(deviceId, user.text, p)
    }

    BField { id: user; Layout.fillWidth: true; label: qsTr("Username"); onAccepted: pw.field.forceActiveFocus() }
    BField { id: pw; Layout.fillWidth: true; label: qsTr("Password"); password: true; onAccepted: root.submit() }

    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Cancel"); onClicked: { pw.text = ""; root.close() } },
        BButton { variant: "primary"; text: qsTr("Connect"); onClicked: root.submit() }
    ]
}
