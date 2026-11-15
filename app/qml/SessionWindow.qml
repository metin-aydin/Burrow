import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Layouts
import "Steps.js" as Steps
import Burrow.Core

// One remote desktop in its own window. It opens as soon as Connect is clicked: the tunnel and the
// RDP connection are shown as one step list, then the desktop takes over. A desktop with several
// monitors shows them as tabs; a tab dragged out of the window opens that monitor in its own window on
// the display it was dropped on. Closing such a window brings the tab back. Which monitors live in their
// own windows (and on which display, full screen or not) is remembered per device.
Window {
    id: win
    property string deviceId
    property var tunnel: ({})             // {id, name, type} or {} for a direct connection
    property RdpSession session: null
    property string failure: ""
    property string frozenStep: ""        // the step that failed
    signal retry(string deviceId)

    readonly property size startSize: App.sessionWindowSize()
    width: startSize.width
    height: startSize.height
    visible: true
    color: Theme.bg
    title: (App.device(deviceId).name || "") + " — Burrow"

    readonly property var tunnelState: tunnel.id ? (App.tunnelStates[tunnel.id] || {}) : ({})
    readonly property bool connecting: !failure && (!session || session.state === "connecting")
    StepLabels { id: labels }

    // --- Monitors ------------------------------------------------------------
    readonly property var monitors: session && session.state === "connected" ? session.monitors : []
    readonly property bool tabbed: monitors.length > 1
    property int currentMonitor: 0
    property var detached: ({})           // monitor index -> MonitorWindow
    property int detachedCount: 0
    property bool restored: false
    property bool ending: false           // the session is closing: don't record windows going away

    function isDetached(i) { return !!detached[i] }
    function attachedMonitors() {
        const out = []
        for (let i = 0; i < monitors.length; ++i) if (!isDetached(i)) out.push(i)
        return out
    }
    function screenAt(gx, gy) {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const sc = screens[i]
            if (gx >= sc.virtualX && gx < sc.virtualX + sc.width && gy >= sc.virtualY && gy < sc.virtualY + sc.height)
                return sc
        }
        return null
    }
    function screenNamed(name) {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) if (screens[i].name === name) return screens[i]
        return null
    }
    // Opens monitor `i` in its own window on `sc` (full screen when `full`).
    function detach(i, sc, full) {
        if (isDetached(i) || i >= monitors.length || attachedMonitors().length <= 1 || !sc) return
        const w = monitorWindow.createObject(null, { session: session, monitor: i,
                                                     deviceName: App.device(deviceId).name || "" })
        w.screen = sc
        const m = monitors[i]
        const fit = Math.min(sc.width * 0.9 / m.width, sc.height * 0.85 / m.height, 1)
        w.width = Math.max(480, m.width * fit)
        w.height = Math.max(320, m.height * fit)
        w.x = sc.virtualX + (sc.width - w.width) / 2
        w.y = sc.virtualY + (sc.height - w.height) / 2
        w.reattach.connect(win.reattach)
        w.layoutChanged.connect(win.scheduleSave)
        if (full) w.showFullScreen(); else w.show()
        const d = Object.assign({}, detached); d[i] = w; detached = d
        detachedCount = Object.keys(d).length
        if (currentMonitor === i) currentMonitor = attachedMonitors()[0]
        scheduleSave()
    }
    function reattach(i) {
        const w = detached[i]
        if (!w) return
        const d = Object.assign({}, detached); delete d[i]; detached = d
        detachedCount = Object.keys(d).length
        Qt.callLater(function() { w.destroy() })
        if (!ending) { currentMonitor = i; scheduleSave() }
    }
    function closeDetached() {
        ending = true
        for (const k in detached) detached[k].destroy()
        detached = ({}); detachedCount = 0
    }
    function scheduleSave() { if (!ending) saveTimer.restart() }
    Timer {
        // Delayed so quitting (which closes every window) doesn't record the windows as closed.
        id: saveTimer
        interval: 800
        onTriggered: {
            if (win.ending || !win.tabbed) return
            const list = []
            for (const k in win.detached) {
                const w = win.detached[k]
                list.push({ monitor: parseInt(k), screen: w.screen ? w.screen.name : "",
                            fullscreen: w.visibility === Window.FullScreen })
            }
            App.saveDeviceLayout(win.deviceId, { detached: list })
        }
    }
    // Puts monitors back where they were last time.
    onTabbedChanged: {
        if (!tabbed || restored) return
        restored = true
        const saved = (App.device(deviceId).layout || {}).detached || []
        for (let j = 0; j < saved.length; ++j) {
            const e = saved[j]
            const sc = screenNamed(e.screen)
            if (sc && e.monitor < monitors.length) detach(e.monitor, sc, !!e.fullscreen)
        }
    }
    Component.onDestruction: closeDetached()
    Component { id: monitorWindow; MonitorWindow {} }

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
    }
    function attach(s) { session = s }

    // Closing the window ends the session (or stops waiting for the tunnel); the window is destroyed
    // once the session reports closed. (Never refuse the close: macOS cancels "Quit" otherwise.)
    onClosing: {
        closeDetached()
        if (session && session.state !== "closed") session.close()
        else { if (!session && !failure) App.cancelConnect(deviceId); win.destroy() }
    }

    // Tabs, one per remote monitor still in this window. Drag one out to give it its own window.
    Rectangle {
        id: tabBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: win.tabbed ? 40 : 0
        visible: win.tabbed
        color: Theme.surface
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: Theme.border }
        Row {
            anchors { left: parent.left; leftMargin: 8; bottom: parent.bottom }
            spacing: 2
            Repeater {
                model: win.monitors.length
                Rectangle {
                    id: tab
                    required property int index
                    readonly property bool current: win.currentMonitor === index
                    readonly property bool away: win.detachedCount >= 0 && win.isDetached(index)
                    visible: !away
                    width: visible ? tabLabel.implicitWidth + 36 : 0
                    height: 32
                    radius: 7
                    color: current ? Theme.bg : tabMouse.containsMouse ? Theme.hover : "transparent"
                    opacity: tabMouse.dragging ? 0.5 : 1
                    // The current tab joins the desktop below it.
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 8
                        color: parent.color
                        visible: tab.current
                    }
                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: qsTr("Display %1").arg(tab.index + 1)
                        color: tab.current ? Theme.text : Theme.textSecondary
                        font.pixelSize: 12
                        font.weight: tab.current ? Font.Medium : Font.Normal
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property point pressAt
                        property bool dragging: false
                        cursorShape: dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        onPressed: (m) => {
                            if (m.button === Qt.RightButton) { moveMenu.monitor = tab.index; moveMenu.popup(); return }
                            win.currentMonitor = tab.index
                            pressAt = Qt.point(m.x, m.y)
                        }
                        onPositionChanged: (m) => {
                            if (!pressed || !(m.buttons & Qt.LeftButton)) return
                            if (!dragging && Math.abs(m.x - pressAt.x) + Math.abs(m.y - pressAt.y) > 8
                                    && win.attachedMonitors().length > 1)
                                dragging = true
                            if (!dragging) return
                            const g = mapToGlobal(m.x, m.y)
                            ghost.label = tabLabel.text
                            ghost.x = g.x - ghost.width / 2
                            ghost.y = g.y - 15
                            ghost.visible = !win.insideWindow(g.x, g.y)
                        }
                        onReleased: (m) => {
                            if (!dragging) return
                            dragging = false
                            ghost.visible = false
                            const g = mapToGlobal(m.x, m.y)
                            if (!win.insideWindow(g.x, g.y))
                                win.detach(tab.index, win.screenAt(g.x, g.y) || win.screen, false)
                        }
                    }
                }
            }
        }
        Text {
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            visible: win.detachedCount >= 0 && win.attachedMonitors().length > 1 && win.width > 760
            text: qsTr("Drag a tab out of the window to show it on another display")
            color: Theme.textMuted
            font.pixelSize: 12
        }
    }
    function insideWindow(gx, gy) { return gx >= x && gx < x + width && gy >= y && gy < y + height }

    // A floating label that follows a tab being dragged out of the window.
    Window {
        id: ghost
        property string label
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowTransparentForInput
        color: "transparent"
        width: ghostLabel.implicitWidth + 36
        height: 30
        Rectangle {
            anchors.fill: parent
            radius: 7
            color: Theme.surfaceRaised
            border.color: Theme.accent
            Text {
                id: ghostLabel
                anchors.centerIn: parent
                text: ghost.label
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }

    // Right-click on a tab: move it to a display without dragging.
    Menu {
        id: moveMenu
        property int monitor: 0
        padding: 4
        background: Rectangle { implicitWidth: 220; radius: Theme.radius; color: Theme.surfaceRaised; border.color: Theme.border }
        Repeater {
            model: Qt.application.screens.length
            MenuItem {
                id: mi
                required property int index
                readonly property var scr: Qt.application.screens[index]
                text: qsTr("Open On %1").arg(scr ? scr.name : "")
                enabled: win.detachedCount >= 0 && win.attachedMonitors().length > 1
                implicitHeight: 32
                contentItem: Text {
                    text: mi.text
                    color: mi.enabled ? Theme.text : Theme.textMuted
                    font.pixelSize: 13
                    leftPadding: 6
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { radius: 6; color: mi.highlighted ? Theme.hover : "transparent" }
                onTriggered: win.detach(moveMenu.monitor, scr, false)
            }
        }
    }

    RdpView {
        id: view
        anchors { left: parent.left; right: parent.right; top: tabBar.bottom; bottom: parent.bottom }
        session: win.session
        monitor: win.tabbed ? win.currentMonitor : -1
        focus: true
        Component.onCompleted: forceActiveFocus()
    }

    // Progress: tunnel and RDP steps in one list, then the error with what to do next.
    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        visible: win.connecting || win.failure !== ""
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
            certDialog.address = address
            certDialog.fingerprint = fingerprint
            certDialog.open()
        }
        function onStateChanged() {
            const s = win.session
            if (!s) return
            if (s.state === "closed") {
                // A connection that never opened keeps the window to show why; otherwise it goes.
                if (s.error !== "" && !s.wasConnected) {
                    win.session = null
                    win.fail(s.error)
                } else {
                    win.closeDetached()
                    win.destroy()
                }
            }
        }
        // The prompt was answered (here or elsewhere) once the connection moves on.
        function onPhaseChanged() { if (win.session && win.session.phase !== "certificate") certDialog.close() }
    }

    Shortcut { sequence: "Ctrl+Meta+F"; onActivated: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen() }
}
