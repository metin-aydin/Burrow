import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Asks for one secret value (VPN password, one-time code) and hands it to a callback.
BDialog {
    id: root
    dialogWidth: 420
    property var callback: null
    property alias label: field.label

    function ask(title, subtitle, label, isPassword, fn) {
        root.title = title
        root.subtitle = subtitle
        field.label = label
        field.password = isPassword
        field.text = ""
        root.callback = fn
        open()
        field.field.forceActiveFocus()
    }
    function submit() {
        if (!field.text) return
        const v = field.text
        field.text = ""
        close()
        if (root.callback) root.callback(v)
    }

    BField { id: field; Layout.fillWidth: true; onAccepted: root.submit() }

    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Cancel"); onClicked: { field.text = ""; root.close() } },
        BButton { variant: "primary"; text: qsTr("Continue"); onClicked: root.submit() }
    ]
}
