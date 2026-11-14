import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Layouts
import "Steps.js" as Steps
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

    // Connecting overlay: what is happening, step by step.
    StepLabels { id: labels }
    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        visible: !!win.session && win.session.state === "connecting"
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Rectangle {
            anchors.centerIn: parent
            width: 380
            height: steps.implicitHeight + 92
            radius: 18
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 16
                RowLayout {
                    spacing: 12
                    Image { source: "../resources/icons/256x256.png"; Layout.preferredWidth: 32; Layout.preferredHeight: 32; sourceSize: Qt.size(64, 64) }
                    Text {
                        Layout.fillWidth: true
                        text: win.session ? win.session.title : ""
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
                ConnectionSteps {
                    id: steps
                    Layout.fillWidth: true
                    steps: Steps.rdpSteps(labels, win.session ? win.session.title : "")
                    current: win.session ? win.session.phase : ""
                    detail: win.session && win.session.phase === "certificate" ? qsTr("Confirm the certificate to continue.") : ""
                }
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
