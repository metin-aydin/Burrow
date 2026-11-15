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
    property RemoteSession session: null
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
    property var tabOrder: []             // monitor indices in tab order (user can reorder)
    property var tabNames: ({})           // monitor index -> name given by the user
    property bool restored: false
    property bool ending: false           // the session is closing: don't record windows going away

    function isDetached(i) { return !!detached[i] }
    // Tabs still in this window, in the user's order.
    readonly property var visibleTabs: {
        detachedCount
        return tabOrder.filter(i => i < monitors.length && !isDetached(i))
    }
    function attachedMonitors() { return visibleTabs }
    function tabName(i) { return tabNames[i] || qsTr("Display %1").arg(i + 1) }
    function renameTab(i, name) {
        const n = Object.assign({}, tabNames)
        const t = name.trim()
        if (t === "" || t === qsTr("Display %1").arg(i + 1)) delete n[i]; else n[i] = t
        tabNames = n
        if (detached[i]) detached[i].displayName = tabName(i)
        scheduleSave()
    }
    function moveTab(monitor, beforeMonitor) {
        const o = tabOrder.filter(i => i !== monitor)
        const at = beforeMonitor < 0 ? o.length : o.indexOf(beforeMonitor)
        o.splice(at < 0 ? o.length : at, 0, monitor)
        if (JSON.stringify(o) !== JSON.stringify(tabOrder)) { tabOrder = o; scheduleSave() }
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
    // Opens monitor `i` in its own window on `sc`; `full` makes it full screen there (after the window
    // has appeared on that display: macOS takes a hidden window full screen on the main display).
    function detach(i, sc, full, delay) {
        if (isDetached(i) || i >= monitors.length || visibleTabs.length <= 1 || !sc) return
        const w = monitorWindow.createObject(null, { session: session, monitor: i,
                                                     deviceName: App.device(deviceId).name || "",
                                                     displayName: tabName(i) })
        w.screen = sc
        const m = monitors[i]
        const fit = Math.min(sc.width * 0.9 / m.width, sc.height * 0.85 / m.height, 1)
        w.width = Math.max(480, m.width * fit)
        w.height = Math.max(320, m.height * fit)
        w.x = sc.virtualX + (sc.width - w.width) / 2
        w.y = sc.virtualY + (sc.height - w.height) / 2
        w.reattach.connect(win.reattach)
        w.layoutChanged.connect(win.scheduleSave)
        w.show()
        if (full) w.goFullScreen(delay || 400)
        const d = Object.assign({}, detached); d[i] = w; detached = d
        detachedCount = Object.keys(d).length
        if (currentMonitor === i) currentMonitor = visibleTabs[0]
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
            if (win.ending) return
            const out = Object.assign({}, App.device(win.deviceId).layout || {})
            // This window: the display it is on and whether it is full screen.
            const ms = win.screen
            out.main = { screen: ms ? ms.name : "", x: ms ? ms.virtualX + ms.width / 2 : 0,
                         y: ms ? ms.virtualY + ms.height / 2 : 0, fullscreen: win.visibility === Window.FullScreen }
            if (!win.tabbed || !win.restored) { App.saveDeviceLayout(win.deviceId, out); return }
            const list = []
            for (const k in win.detached) {
                const w = win.detached[k]
                // The display is found again by a point on it (names can repeat), the name is a fallback.
                const sc = w.screen
                list.push({ monitor: parseInt(k), screen: sc ? sc.name : "",
                            x: sc ? sc.virtualX + sc.width / 2 : 0, y: sc ? sc.virtualY + sc.height / 2 : 0,
                            fullscreen: w.visibility === Window.FullScreen })
            }
            out.detached = list
            // The size each monitor's picture has (its own window, or the tab view): the next connection
            // starts with exactly these, as some servers can't change them while connected.
            const k = win.session ? win.session.scale / 100 : 1
            const sizes = win.monitors.map((m, i) => {
                const d = win.detached[i]
                const w = d ? d.viewSize.width : view.width, h = d ? d.viewSize.height : view.height
                return [Math.round(w * k) & ~1, Math.round(h * k)]
            })
            if (sizes.length > 0 && sizes.every(s => s[0] >= 200 && s[1] >= 200))
                out.sizes = sizes
            out.order = win.tabOrder
            out.names = win.tabNames
            App.saveDeviceLayout(win.deviceId, out)
        }
    }
    // Puts tabs and windows back the way they were last time.
    onTabbedChanged: {
        if (!tabbed || restored) return
        const layout = App.device(deviceId).layout || {}
        const n = monitors.length
        const order = (layout.order || []).filter(i => i >= 0 && i < n)
        for (let i = 0; i < n; ++i) if (order.indexOf(i) < 0) order.push(i)
        tabOrder = order
        const names = {}
        const saved = layout.names || {}
        for (const k in saved) if (parseInt(k) < n && saved[k]) names[k] = saved[k]
        tabNames = names
        currentMonitor = order[0]
        restored = true
        const list = layout.detached || []
        let k = 0
        for (let j = 0; j < list.length; ++j) {
            const e = list[j]
            const sc = (e.x !== undefined ? screenAt(e.x, e.y) : null) || screenNamed(e.screen)
            if (!sc || e.monitor >= n) continue
            // One display at a time going full screen; macOS animates each.
            detach(e.monitor, sc, !!e.fullscreen, 500 + 700 * k)
            if (e.fullscreen) ++k
        }
    }
    Component.onDestruction: closeDetached()
    // Opens where it was left last time (display, full screen), centred on that display; a new device
    // opens centred on the main display.
    Component.onCompleted: {
        const saved = (App.device(deviceId).layout || {}).main || {}
        const sc = (saved.x !== undefined ? screenAt(saved.x, saved.y) : null) || screenNamed(saved.screen || "")
                   || screen
        if (sc) {
            screen = sc
            x = sc.virtualX + (sc.width - width) / 2
            y = sc.virtualY + Math.max(0, sc.height - height) / 2
        }
        if (saved.fullscreen) fullTimer.start()
    }
    // After the window has appeared on its display (macOS takes hidden windows full screen on the main one).
    Timer { id: fullTimer; interval: 300; onTriggered: win.showFullScreen() }
    onVisibilityChanged: scheduleSave()
    onScreenChanged: scheduleSave()
    Component { id: monitorWindow; MonitorWindow {} }

    function stepList() {
        const name = App.device(deviceId).name || ""
        // VNC (Macs) has no certificate step.
        const vnc = App.device(deviceId).protocol === "vnc"
        const rdp = Steps.rdpSteps(labels, name).filter(s => !vnc || s.id !== "certificate")
                         .map(s => ({ id: "r_" + s.id, label: s.label }))
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

    // Tabs, one per remote monitor still in this window: click to switch, drag sideways to reorder,
    // drag out of the window to open it on another display, double-click to rename. Hidden when only
    // one monitor is left here.
    readonly property bool showTabs: tabbed && visibleTabs.length > 1
    FontMetrics { id: tabMetrics; font.pixelSize: 12; font.weight: Font.Medium }
    function tabWidth(i) { return Math.ceil(tabMetrics.advanceWidth(tabName(i))) + 36 }
    function tabX(i) {
        let x = 0
        for (let j = 0; j < visibleTabs.length && visibleTabs[j] !== i; ++j) x += tabWidth(visibleTabs[j]) + 2
        return x
    }
    // The tab under x (in tab-strip coordinates), or -1 past the last one.
    function tabAtX(x) {
        let pos = 0
        for (let j = 0; j < visibleTabs.length; ++j) {
            const w = tabWidth(visibleTabs[j])
            if (x < pos + w / 2) return visibleTabs[j]
            pos += w + 2
        }
        return -1
    }
    Rectangle {
        id: tabBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: win.showTabs ? 40 : 0
        visible: win.showTabs
        color: Theme.surface
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: Theme.border }
        Item {
            id: strip
            anchors { left: parent.left; leftMargin: 8; right: parent.right; top: parent.top; bottom: parent.bottom }
            Repeater {
                model: win.monitors.length
                Rectangle {
                    id: tab
                    required property int index
                    readonly property bool current: win.currentMonitor === index
                    readonly property bool shown: win.visibleTabs.indexOf(index) >= 0
                    readonly property bool editing: editor.visible
                    visible: shown
                    x: tabMouse.dragging ? tabMouse.dragX : win.tabX(index)
                    Behavior on x { enabled: !tabMouse.dragging; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                    z: tabMouse.dragging ? 2 : 1
                    y: parent.height - height
                    width: win.tabWidth(index)
                    height: 32
                    radius: 7
                    color: current ? Theme.bg : tabMouse.containsMouse ? Theme.hover : "transparent"
                    border.color: tabMouse.dragging ? Theme.accent : "transparent"
                    // The current tab joins the desktop below it.
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 8
                        color: parent.color
                        visible: tab.current && !tabMouse.dragging
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !tab.editing
                        text: win.tabName(tab.index)
                        color: tab.current ? Theme.text : Theme.textSecondary
                        font.pixelSize: 12
                        font.weight: tab.current ? Font.Medium : Font.Normal
                    }
                    TextInput {
                        id: editor
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        visible: false
                        verticalAlignment: TextInput.AlignVCenter
                        horizontalAlignment: TextInput.AlignHCenter
                        color: Theme.text
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.accentText
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        maximumLength: 40
                        function begin() {
                            text = win.tabName(tab.index); visible = true; forceActiveFocus(); selectAll()
                            win.tabEditor = editor
                        }
                        function finish(save) {
                            if (!visible) return
                            visible = false
                            if (win.tabEditor === editor) win.tabEditor = null
                            if (save) win.renameTab(tab.index, text)
                            view.forceActiveFocus()
                        }
                        Keys.onReturnPressed: finish(true)
                        Keys.onEnterPressed: finish(true)
                        Keys.onEscapePressed: finish(false)
                        // Leaving the field any other way (another app, a click elsewhere) works like Esc.
                        onActiveFocusChanged: if (!activeFocus) finish(false)
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        enabled: !tab.editing
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property point pressAt
                        property bool dragging: false
                        property real dragX: 0
                        cursorShape: dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        onPressed: (m) => {
                            if (m.button === Qt.RightButton) { tabMenu.monitor = tab.index; tabMenu.popup(); return }
                            win.currentMonitor = tab.index
                            pressAt = Qt.point(m.x, m.y)
                        }
                        onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) editor.begin() }
                        onPositionChanged: (m) => {
                            if (!pressed || !(m.buttons & Qt.LeftButton)) return
                            if (!dragging && Math.abs(m.x - pressAt.x) + Math.abs(m.y - pressAt.y) > 6) {
                                dragging = true
                                dragX = tab.x
                            }
                            if (!dragging) return
                            const g = mapToGlobal(m.x, m.y)
                            const outside = !win.insideWindow(g.x, g.y)
                            // Inside the window: the tab follows the pointer along the strip and the
                            // others make room. Outside: a floating label follows the pointer.
                            const p = mapToItem(strip, m.x, m.y)
                            dragX = Math.max(0, p.x - pressAt.x)
                            if (!outside) {
                                const over = win.tabAtX(p.x)
                                if (over !== tab.index) win.moveTab(tab.index, over)
                            }
                            ghost.label = win.tabName(tab.index)
                            ghost.x = g.x - ghost.width / 2
                            ghost.y = g.y - 15
                            ghost.visible = outside && win.visibleTabs.length > 1
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
            id: hint
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            visible: win.width > 900
            text: qsTr("Drag a tab out of the window to show it on another display")
            color: Theme.textMuted
            font.pixelSize: 12
        }
    }
    // The tab name being edited, if any: a click anywhere else ends the edit like Esc, and the click
    // still reaches what was clicked.
    property Item tabEditor: null
    MouseArea {
        anchors.fill: parent
        z: 1000
        visible: win.tabEditor !== null
        acceptedButtons: Qt.AllButtons
        onPressed: (m) => {
            const e = win.tabEditor
            const p = mapToItem(e, m.x, m.y)
            if (!e.contains(p)) e.finish(false)
            m.accepted = false
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

    // Right-click on a tab: rename it or open it on a display without dragging.
    Menu {
        id: tabMenu
        property int monitor: 0
        padding: 4
        background: Rectangle { implicitWidth: 220; radius: Theme.radius; color: Theme.surfaceRaised; border.color: Theme.border }
        component TabMenuItem: MenuItem {
            id: mi
            implicitHeight: 32
            contentItem: Text {
                text: mi.text
                color: mi.enabled ? Theme.text : Theme.textMuted
                font.pixelSize: 13
                leftPadding: 6
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { radius: 6; color: mi.highlighted ? Theme.hover : "transparent" }
        }
        TabMenuItem {
            text: qsTr("Rename")
            onTriggered: {
                const t = strip.children.find(c => c.index === tabMenu.monitor)
                if (t) t.children.find(c => c.begin !== undefined).begin()
            }
        }
        Repeater {
            model: Qt.application.screens.length
            TabMenuItem {
                required property int index
                readonly property var scr: Qt.application.screens[index]
                text: qsTr("Open On %1").arg(scr ? scr.name : "")
                enabled: win.visibleTabs.length > 1
                onTriggered: win.detach(tabMenu.monitor, scr, false)
            }
        }
    }

    RdpView {
        id: view
        anchors { left: parent.left; right: parent.right; top: win.showTabs ? tabBar.bottom : parent.top; bottom: parent.bottom }
        session: win.session
        monitor: win.tabbed ? win.currentMonitor : -1
        onWidthChanged: win.scheduleSave()
        onHeightChanged: win.scheduleSave()
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
        function onDesktopSizeChanged() { win.scheduleSave() }
        function onPhaseChanged() { if (win.session && win.session.phase !== "certificate") certDialog.close() }
    }

    Shortcut { sequence: "Ctrl+Meta+F"; onActivated: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen() }
}
