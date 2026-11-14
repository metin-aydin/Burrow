import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Burrow.Core

// One remote desktop in its own window.
Window {
    id: win
    required property RdpSession session
    width: 1280
    height: 820
    visible: true
    color: "black"
    title: session.title + " — Burrow"

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
        visible: win.session.state === "connecting"
        Column {
            anchors.centerIn: parent
            spacing: 14
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: parent.visible }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Connecting to %1…").arg(win.session.title)
                color: Theme.text
                font.pixelSize: 15
            }
        }
    }

    Connections {
        target: win.session
        function onStateChanged() {
            if (win.session.state === "closed")
                win.destroy()
        }
    }

    Shortcut { sequence: "Ctrl+Meta+F"; onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
}
