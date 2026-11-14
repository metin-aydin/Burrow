import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Layouts
import Burrow.Core

// One remote desktop in its own window.
Window {
    id: win
    required property RdpSession session
    width: 1280
    height: 820
    visible: true
    color: "black"
    title: (session ? session.title + " — " : "") + "Burrow"

    // Closing the window ends the session; the window is destroyed once the session reports closed.
    // (Never refuse the close: macOS cancels "Quit" when a window rejects it.)
    onClosing: if (session && session.state !== "closed") session.close()

    RdpView {
        id: view
        anchors.fill: parent
        session: win.session
        focus: true
        Component.onCompleted: forceActiveFocus()
    }

    // Connecting overlay
    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        visible: !!win.session && win.session.state === "connecting"
        Column {
            anchors.centerIn: parent
            spacing: 14
            Spinner { anchors.horizontalCenter: parent.horizontalCenter; size: 44; running: parent.visible }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Connecting to %1…").arg(win.session ? win.session.title : "")
                color: Theme.text
                font.pixelSize: 15
            }
        }
    }

    // Certificate confirmation on the first connection (or after the address changed).
    BDialog {
        id: certDialog
        dialogWidth: 480
        title: qsTr("Trust This Computer?")
        property string address
        property string fingerprint
        subtitle: qsTr("This is the first connection to %1. Check that the fingerprint matches the computer's certificate, then trust it. Burrow refuses to connect if it changes later.").arg(address)
        Text {
            Layout.fillWidth: true
            text: certDialog.fingerprint
            color: Theme.text
            font.family: "Menlo"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
        }
        footer: [
            Item { Layout.fillWidth: true },
            BButton { text: qsTr("Don't Connect"); onClicked: { certDialog.close(); if (win.session) win.session.answerCertificate(false) } },
            BButton { variant: "primary"; text: qsTr("Trust And Connect"); onClicked: { certDialog.close(); if (win.session) win.session.answerCertificate(true) } }
        ]
    }

    Connections {
        target: win.session
        function onCertificatePrompt(address, fingerprint) {
            certDialog.address = address
            certDialog.fingerprint = fingerprint
            certDialog.open()
        }
        function onStateChanged() {
            if (!win.session || win.session.state === "closed")
                win.destroy()
        }
    }

    Shortcut { sequence: "Ctrl+Meta+F"; onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
}
