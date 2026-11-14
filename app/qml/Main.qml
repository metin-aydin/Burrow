import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: "Burrow"
    color: Theme.bg
    font.family: Theme.fontFamily

    Loader {
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
    ConnectProgress { id: connectProgress; onRetry: (id) => win.startSession(id, "", "") }
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
    TunnelsDialog { id: tunnels; onConfirm: (t, m, b, f) => confirm.ask(t, m, b, f); onEditTunnel: (id) => tunnelEditor.openFor(id) }
    TunnelEditor { id: tunnelEditor }
    SettingsDialog { id: settings }
    ConfirmDialog { id: confirm }
    Toast { id: toast }

    // Development: open a dialog on start for screenshots (--seed-demo + BURROW_DEMO_OPEN).
    Timer {
        interval: 600; running: App.demoOpen !== "" && App.state === "unlocked"
        onTriggered: {
            if (App.demoOpen === "device") deviceEditor.openFor(App.gallery.count ? "" : "")
            else if (App.demoOpen === "tunnels") tunnels.open()
            else if (App.demoOpen === "settings") settings.open()
            else if (App.demoOpen === "tunnel") tunnelEditor.openFor(App.tunnels.count ? "" : "")
            else if (App.demoOpen === "tunnel-otp") { tunnelEditor.openFor(""); tunnelEditor.demoExpand() }
            else if (App.demoOpen === "tunnel-test") {
                for (let i = 0; i < App.tunnels.count; ++i) {
                    const id = App.tunnels.data(App.tunnels.index(i, 0), 257)
                    if (App.tunnel(id).type === "tailscale") { tunnelEditor.openFor(id); tunnelEditor.startTest() }
                }
            }
            else if (App.demoOpen === "tunnel-cf") { tunnelEditor.openFor(""); tunnelEditor.demoCloudflare() }
            else if (App.demoOpen === "tunnel-otp-small") { win.height = 560; tunnelEditor.openFor(""); tunnelEditor.demoExpand() }
        }
    }

    Connections {
        target: App
        function onToast(message) { toast.show(message) }
        function onSessionOpened(session) {
            if (connectProgress.visible && connectProgress.deviceId === session.deviceId)
                connectProgress.close()
            sessionWindow.createObject(null, { session: session })
        }
        function onDeviceConnecting(deviceId, tunnelId) { connectProgress.show(deviceId) }
        function onDeviceConnectFailed(deviceId, error) { connectProgress.fail(deviceId, error) }
        function onTunnelLoginRequired(tunnelId, name, url) { tsLogin.show(name, url) }
        function onTunnelPasswordRequired(tunnelId, name) { win.askTunnelPassword(tunnelId, name, "", "", "") }
        function onTunnelOtpRequired(tunnelId, name, label) {
            prompt.ask(qsTr("One-Time Code For %1").arg(name), label || qsTr("Enter the code from your authenticator app."),
                       qsTr("Code"), false, function(code) { App.submitTunnelOtp(tunnelId, code) })
        }
        function onStateChanged() {
            if (App.state !== "unlocked") {
                deviceEditor.close(); tunnels.close(); tunnelEditor.close(); prompt.close(); tsLogin.close(); connectProgress.close(); settings.close(); confirm.close()
            }
        }
    }

    Shortcut { sequence: "Ctrl+L"; enabled: App.state === "unlocked"; onActivated: App.lock() }
    Shortcut { sequences: [StandardKey.New]; enabled: App.state === "unlocked"; onActivated: deviceEditor.openFor("") }
}
