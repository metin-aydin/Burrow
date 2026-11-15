import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    // Wide enough for the gallery header (it depends on the language and an update badge), so no
    // button is ever cut off.
    minimumWidth: Math.max(760, Math.ceil(content.item && content.item.minimumContentWidth ? content.item.minimumContentWidth : 0))
    minimumHeight: 520
    visible: true
    title: "Burrow"
    color: Theme.bg
    font.family: Theme.fontFamily
    // The content runs under the title bar (our header is in it), so no automatic safe-area padding.
    topPadding: 0
    leftPadding: 0
    rightPadding: 0

    // Our header is the title bar: its empty parts move the window, a double-click zooms it. Declared
    // under the content so buttons in the header still get their clicks.
    Item {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Math.max(Theme.titleBarHeight, 1)
        visible: Theme.contentTitleBar
        DragHandler {
            target: null
            onActiveChanged: if (active) win.startSystemMove()
        }
        TapHandler {
            onDoubleTapped: win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized()
        }
    }

    Loader {
        id: content
        anchors.fill: parent
        sourceComponent: App.state === "unlocked" ? gallery : lockScreen
    }

    Component { id: lockScreen; LockScreen {} }
    Component {
        id: gallery
        GalleryView {
            onNewDevice: deviceEditor.openFor("")
            onEditDevice: (id) => deviceEditor.openFor(id)
            onOpenTunnels: tunnels.open()
            onOpenSettings: settings.open()
            onConnectDevice: (id) => win.startSession(id, "", "")
            onConfirmDelete: (id, name) => confirm.ask(
                qsTr("Delete Device"),
                qsTr("\"%1\" and its saved preview will be deleted. This can't be undone.").arg(name),
                qsTr("Delete"), function() { App.deleteDevice(id) })
        }
    }

    DeviceEditor { id: deviceEditor }
    CredentialsDialog {
        id: credentials
        onSubmitted: (id, user, pass) => win.startSession(id, user, pass)
    }
    Component { id: sessionWindow; SessionWindow {} }

    PromptDialog { id: prompt }

    // deviceId -> its session window (the one with the progress; extra displays aren't listed).
    property var sessionWindows: ({})
    function liveWindow(id) {
        const w = sessionWindows[id]
        return w && w.deviceId === id ? w : null
    }
    function windowFor(id) {
        let w = liveWindow(id)
        if (w) return w
        w = sessionWindow.createObject(null, { deviceId: id, tunnel: App.deviceTunnel(id) })
        sessionWindows[id] = w
        w.retry.connect(rid => win.startSession(rid, "", ""))
        return w
    }
    TailscaleLoginDialog { id: tsLogin }

    function askTunnelPassword(tunnelId, name, retryDevice, user, pass) {
        prompt.ask(qsTr("Sign In To %1").arg(name), qsTr("Enter your VPN password. It is kept in memory until Burrow locks."),
                   qsTr("VPN Password"), true, function(pw) {
            const err = App.setTunnelPassword(tunnelId, pw)
            if (err) { toast.show(err); return }
            if (retryDevice) win.startSession(retryDevice, user, pass)
        })
    }

    function startSession(id, user, pass) {
        if (App.state !== "unlocked") { win.show(); return }
        const open = liveWindow(id)
        if (open && open.failure === "") { open.raise(); open.requestActivate(); return }
        if (open) open.destroy()
        const r = App.connectDevice(id, user || "", pass || "")
        if (r === "credentials") {
            const d = App.device(id)
            credentials.ask(id, d.name, d.username)
        } else if (r.indexOf("tunnel_password:") === 0) {
            const tid = r.substring("tunnel_password:".length)
            askTunnelPassword(tid, App.tunnelName(tid), id, user, pass)
        } else if (r) {
            toast.show(r)
        }
    }
    TunnelsDialog { id: tunnels; onConfirm: (t, m, b, f) => confirm.ask(t, m, b, f); onEditTunnel: (id) => tunnelEditor.openFor(id); onImportDevices: (id) => importDevices.show(id) }
    ImportDevicesDialog { id: importDevices; toastTarget: toast }
    TunnelEditor { id: tunnelEditor; onImportRequested: (id) => importDevices.show(id) }
    SettingsDialog { id: settings }
    ConfirmDialog { id: confirm }
    Toast { id: toast }

    // Development: open a dialog on start for screenshots (--seed-demo + BURROW_DEMO_OPEN).
    Timer {
        interval: 600; running: App.demoOpen !== "" && App.state === "unlocked"
        onTriggered: {
            if (App.demoOpen === "device") deviceEditor.openFor(App.gallery.count ? "" : "")
            else if (App.demoOpen === "device-mac") { deviceEditor.openFor(""); deviceEditor.demoMac() }
            else if (App.demoOpen === "tunnels") tunnels.open()
            else if (App.demoOpen === "settings") settings.open()
            else if (App.demoOpen === "combo") { settings.open(); Qt.callLater(settings.demoCombo) }
            else if (App.demoOpen === "tunnel") tunnelEditor.openFor(App.tunnels.count ? "" : "")
            else if (App.demoOpen === "tunnel-qr") { tunnelEditor.openFor(""); tunnelEditor.demoExpand(); Qt.callLater(tunnelEditor.demoQrMenu) }
            else if (App.demoOpen === "tunnel-otp") { tunnelEditor.openFor(""); tunnelEditor.demoExpand() }
            else if (App.demoOpen === "tunnel-test") {
                for (let i = 0; i < App.tunnels.count; ++i) {
                    const id = App.tunnels.data(App.tunnels.index(i, 0), 257)
                    if (App.tunnel(id).type === "tailscale") { tunnelEditor.openFor(id); tunnelEditor.startTest() }
                }
            }
            else if (App.demoOpen === "nomatch") content.item.demoSearch("zzz")
            else if (App.demoOpen === "tunnel-cf") { tunnelEditor.openFor(""); tunnelEditor.demoCloudflare() }
            else if (App.demoOpen === "tunnel-ts") { tunnelEditor.openFor(""); tunnelEditor.demoTailscale() }
            else if (App.demoOpen === "tunnel-otp-small") { win.height = 560; tunnelEditor.openFor(""); tunnelEditor.demoExpand() }
        }
    }

    Connections {
        target: App
        function onToast(message) { toast.show(message) }
        function onSessionOpened(session) {
            let w = win.liveWindow(session.deviceId)
            if (w && w.session) w = null
            if (!w) { if (win.liveWindow(session.deviceId)) win.liveWindow(session.deviceId).destroy(); w = win.windowFor(session.deviceId) }
            w.attach(session)
        }
        function onDeviceConnecting(deviceId, tunnelId) { win.windowFor(deviceId) }
        function onDeviceConnectFailed(deviceId, error) {
            const w = win.liveWindow(deviceId)
            if (w) w.fail(error); else toast.show(error)
        }
        function onTunnelLoginRequired(tunnelId, name, url) { tsLogin.show(name, url) }
        function onTunnelPasswordRequired(tunnelId, name) { win.askTunnelPassword(tunnelId, name, "", "", "") }
        function onTunnelOtpRequired(tunnelId, name, label) {
            prompt.ask(qsTr("One-Time Code For %1").arg(name), label || qsTr("Enter the code from your authenticator app."),
                       qsTr("Code"), false, function(code) { App.submitTunnelOtp(tunnelId, code) })
        }
        function onStateChanged() {
            if (App.state !== "unlocked") {
                deviceEditor.close(); tunnels.close(); tunnelEditor.close(); prompt.close(); tsLogin.close(); importDevices.close(); settings.close(); confirm.close()
            }
        }
    }

    Shortcut { sequence: "Ctrl+L"; enabled: App.state === "unlocked"; onActivated: App.lock() }
    Shortcut { sequences: [StandardKey.New]; enabled: App.state === "unlocked"; onActivated: deviceEditor.openFor("") }
    // Standard Mac keys for the gallery; not while a dialog is in front of it.
    readonly property bool dialogOpen: deviceEditor.opened || credentials.opened || prompt.opened || tsLogin.opened
        || tunnels.opened || importDevices.opened || tunnelEditor.opened || settings.opened || confirm.opened
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: App.state === "unlocked" && !win.dialogOpen
        onActivated: content.item.focusSearch()
    }
    Shortcut {
        sequences: [StandardKey.Preferences]
        enabled: App.state === "unlocked" && !win.dialogOpen
        onActivated: settings.open()
    }
}
