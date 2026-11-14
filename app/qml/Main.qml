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

    function startSession(id, user, pass) {
        const r = App.connectDevice(id, user || "", pass || "")
        if (r === "credentials") {
            const d = App.device(id)
            credentials.ask(id, d.name, d.username)
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
        }
    }

    Connections {
        target: App
        function onToast(message) { toast.show(message) }
        function onSessionOpened(session) { sessionWindow.createObject(null, { session: session }) }
        function onStateChanged() {
            if (App.state !== "unlocked") {
                deviceEditor.close(); tunnels.close(); tunnelEditor.close(); settings.close(); confirm.close()
            }
        }
    }

    Shortcut { sequence: "Ctrl+L"; enabled: App.state === "unlocked"; onActivated: App.lock() }
    Shortcut { sequences: [StandardKey.New]; enabled: App.state === "unlocked"; onActivated: deviceEditor.openFor("") }
}
