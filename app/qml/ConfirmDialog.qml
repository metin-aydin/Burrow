import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

BDialog {
    id: root
    dialogWidth: 420
    property string message
    property string confirmText
    property var onConfirm: null

    function ask(title, message, confirmText, fn) {
        root.title = title; root.message = message; root.confirmText = confirmText
        root.onConfirm = fn
        open()
    }

    Text {
        Layout.fillWidth: true
        text: root.message
        color: Theme.textSecondary
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
    }
    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Cancel"); onClicked: root.close() },
        BButton { variant: "danger"; text: root.confirmText; onClicked: { root.close(); if (root.onConfirm) root.onConfirm() } }
    ]
}
