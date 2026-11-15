import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Layouts
import "Steps.js" as Steps
import Burrow.Core

// One remote desktop in its own window. It opens as soon as Connect is clicked: the tunnel and the
// RDP connection are shown as one step list, then the desktop takes over. With "All Displays" the
// session spans every Mac screen: this window shows monitor 0 and Main creates one per other screen.
Window {
    id: win
    property string deviceId
    property var tunnel: ({})             // {id, name, type} or {} for a direct connection
    property RdpSession session: null
    property int monitorIndex: -1         // -1 = whole desktop; ≥0 = that display of a spanned session
    readonly property bool primary: monitorIndex <= 0
    property string failure: ""
    property string frozenStep: ""        // the step that failed
    signal retry(string deviceId)
    signal spanRequested(RdpSession session)

    readonly property size startSize: App.sessionWindowSize()
    width: startSize.width
    height: startSize.height
    visible: true
    color: primary ? Theme.bg : "black"
    title: (App.device(deviceId).name || "") + " — Burrow"

    readonly property var tunnelState: tunnel.id ? (App.tunnelStates[tunnel.id] || {}) : ({})
    readonly property bool connecting: !failure && (!session || session.state === "connecting")
    StepLabels { id: labels }

    function stepList() {
        const name = App.device(deviceId).name || ""
        const rdp = Steps.rdpSteps(labels, name).map(s => ({ id: "r_" + s.id, label: s.label }))
        if (!tunnel.id) return rdp
        return Steps.tunnelSteps(tunnel.type || "", labels).map(s => ({ id: "t_" + s.id, label: s.label })).concat(rdp)
    }
    function currentStep() {
        if (session) return session.phase === "connected" ? "done" : "r_" + session.phase
        if (!tunnel.id) return "r_connecting"
        const c = Steps.tunnelCurrent(tunnelState)
        if (c === "done") return "r_connecting"
        return "t_" + (c || Steps.tunnelSteps(tunnel.type || "", labels)[0].id)
    }
    function fail(message) {
        frozenStep = currentStep()
        failure = message || qsTr("The connection failed.")
        if (visibility === Window.FullScreen) showNormal()
    }
    function attach(s) {
        session = s
        const mons = s.monitors
        if (mons.length > 1 && monitorIndex < 0) {
            monitorIndex = 0
            const sc = Qt.application.screens[mons[0].screen]
            if (sc) screen = sc
            showFullScreen()
            spanRequested(s)
        }
    }

    // The server ignored the multi-monitor layout (e.g. GNOME screen sharing shows one display):
    // extra windows go, the main one shows the whole desktop in a normal window.
    function checkSpan() {
        const s = session
        if (!s || monitorIndex < 0 || s.state !== "connected" || s.multiMonitor) return
        if (monitorIndex > 0) { win.destroy(); return }
        monitorIndex = -1
        showNormal()
    }

    // Closing the window ends the session (or stops waiting for the tunnel); the window is destroyed
    // once the session reports closed. (Never refuse the close: macOS cancels "Quit" otherwise.)
    onClosing: {
        if (session && session.state !== "closed") session.close()
        else { if (!session && !failure) App.cancelConnect(deviceId); win.destroy() }
    }

    RdpView {
        id: view
        anchors.fill: parent
        session: win.session
        monitor: win.monitorIndex
        focus: true
        Component.onCompleted: forceActiveFocus()
    }

    // Progress: tunnel and RDP steps in one list, then the error with what to do next.
    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        visible: win.primary && (win.connecting || win.failure !== "")
        Rectangle {
            id: card
            anchors.horizontalCenter: parent.horizontalCenter
            // A fixed top edge: the card grows downwards only, so the steps never move.
            y: Math.max(24, parent.height / 2 - 190)
            width: Math.min(parent.width - 48, 420)
            height: content.implicitHeight + 56
            radius: Theme.radiusLarge
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: content
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
                spacing: 18
                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: (win.failure ? qsTr("Couldn't Connect To %1") : qsTr("Connecting %1"))
                              .arg(App.device(win.deviceId).name || "")
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!win.tunnel.id
                        text: qsTr("Through %1").arg(win.tunnel.name || "")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                ConnectionSteps {
                    Layout.fillWidth: true
                    steps: win.stepList()
                    current: win.failure ? win.frozenStep : win.currentStep()
                    failed: win.failure !== ""
                    wait: !win.session ? (win.tunnelState.wait || 0) : 0
                    detail: win.session && win.session.phase === "certificate" ? qsTr("Confirm the certificate to continue.")
                          : !win.session && win.tunnelState.state === "needs_login" ? qsTr("Finish signing in in your browser.")
                          : ""
                }
                Text {
                    visible: win.failure !== ""
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: win.failure
                    color: Theme.danger
                    font.pixelSize: 13
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    BButton {
                        text: win.failure ? qsTr("Close") : qsTr("Cancel")
                        onClicked: win.close()
                    }
                    BButton {
                        visible: win.failure !== ""
                        variant: "primary"
                        text: qsTr("Try Again")
                        onClicked: { const id = win.deviceId; win.destroy(); win.retry(id) }
                    }
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
            if (!win.primary) return
            certDialog.address = address
            certDialog.fingerprint = fingerprint
            certDialog.open()
        }
        function onStateChanged() {
            const s = win.session
            if (!s) return
            if (s.state === "connected") win.checkSpan()
            if (s.state === "closed") {
                // A connection that never opened keeps the window to show why; otherwise it goes.
                if (win.primary && s.error !== "" && !s.wasConnected) {
                    win.session = null
                    win.fail(s.error)
                } else {
                    win.destroy()
                }
            }
        }
        function onDesktopSizeChanged() { win.checkSpan() }
        // The prompt was answered (here or elsewhere) once the connection moves on.
        function onPhaseChanged() { if (win.session && win.session.phase !== "certificate") certDialog.close() }
    }

    Shortcut { sequence: "Ctrl+Meta+F"; onActivated: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen() }
}
