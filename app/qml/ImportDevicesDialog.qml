import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Burrow.Core

// Adds devices from a tunnel's network: Tailscale peers, or Cloudflare Access applications.
BDialog {
    id: root
    dialogWidth: 520
    property string tunnelId
    property string type
    property var items: []         // [{name, host, os, detail, online, checked}]
    property bool loading: false
    property string error
    readonly property var st: App.tunnelStates[tunnelId] || {}

    function show(id) {
        tunnelId = id
        const t = App.tunnel(id)
        type = t.type
        items = []
        error = ""
        tokenField.text = ""
        title = qsTr("Import Devices")
        subtitle = type === "tailscale"
                   ? qsTr("Machines in your tailnet. Pick the ones to add to Burrow.")
                   : qsTr("Access applications in your Cloudflare account. Cloudflare lets apps read them only with an API token: create one with the button below (the right permission is already chosen), copy it, and come back. Burrow uses it once and doesn't keep it.")
        open()
        if (type === "tailscale") {
            loading = true
            App.openTunnelForImport(id)
        }
    }
    onClosed: App.stopTunnelTest(tunnelId)

    Connections {
        target: App
        function onTunnelStatesChanged() {
            if (!root.visible || root.type !== "tailscale" || !root.loading) return
            if (root.st.state === "connected") {
                const peers = App.tunnelPeers(root.tunnelId)
                root.items = peers.map(p => ({ name: p.name, host: p.dns || p.ip, os: p.os,
                                               detail: (p.dns || p.ip) + (p.os ? " · " + p.os : ""),
                                               online: p.online, checked: p.online }))
                root.loading = false
                if (root.items.length === 0) root.error = qsTr("No other machines found in the tailnet.")
            } else if (root.st.state === "failed") {
                root.loading = false
                root.error = root.st.error || qsTr("The tunnel couldn't be opened.")
            }
        }
        function onCloudflareAppsReady(result) {
            if (!root.visible) return
            root.loading = false
            if (result.error) { root.error = result.error; return }
            root.items = (result.apps || []).map(a => ({ name: a.name, host: a.domain, os: "windows",
                                                         detail: a.domain, online: true, checked: true }))
            if (root.items.length === 0) root.error = qsTr("No Access applications found.")
        }
    }

    // Cloudflare: open the prefilled token page, then take the copied token from the clipboard
    // (automatically when Burrow comes back to the front, or with Paste Token).
    function tryClipboard(manual) {
        if (root.type !== "cloudflare" || root.loading || root.items.length > 0) return
        if (App.listCloudflareAppsFromClipboard()) { root.error = ""; root.loading = true }
        else if (manual) root.error = qsTr("No API token on the clipboard. Copy the token Cloudflare shows after you create it.")
    }
    Connections {
        target: Qt.application
        function onStateChanged() { if (root.visible && Qt.application.state === Qt.ApplicationActive) root.tryClipboard(false) }
    }
    RowLayout {
        visible: root.type === "cloudflare" && root.items.length === 0
        spacing: 8
        BButton {
            variant: "primary"
            iconName: "cloud"
            text: qsTr("Create Token In Cloudflare")
            onClicked: Qt.openUrlExternally(App.cloudflareTokenUrl())
        }
        BButton { iconName: "copy"; text: qsTr("Paste Token"); onClicked: root.tryClipboard(true) }
        Item { Layout.fillWidth: true }
    }
    RowLayout {
        visible: root.type === "cloudflare" && root.items.length === 0
        spacing: 8
        BField { id: tokenField; Layout.fillWidth: true; label: qsTr("Or Enter The Token"); password: true; onAccepted: listBtn.clicked() }
        BButton {
            id: listBtn
            Layout.alignment: Qt.AlignBottom
            enabled: tokenField.text.trim() !== ""
            text: qsTr("List Applications")
            onClicked: { root.error = ""; root.loading = true; App.listCloudflareApps(tokenField.text.trim()); tokenField.text = "" }
        }
    }

    RowLayout {
        visible: root.loading
        spacing: 12
        Spinner { size: 22 }
        Text {
            text: root.type === "tailscale" && root.st.state === "needs_login" ? qsTr("Sign in to Tailscale in your browser…")
                 : qsTr("Loading…")
            color: Theme.textSecondary
            font.pixelSize: 13
        }
    }

    Repeater {
        model: root.items
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            radius: Theme.radius
            color: rowMouse.containsMouse ? Theme.hover : Theme.surfaceSunken
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: { const a = root.items.slice(); a[index].checked = !a[index].checked; root.items = a }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                CheckBox { checked: modelData.checked; onToggled: { const a = root.items.slice(); a[index].checked = checked; root.items = a } }
                Rectangle { width: 8; height: 8; radius: 4; color: modelData.online ? Theme.success : Theme.borderStrong }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { text: modelData.name; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { text: modelData.detail; color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
        }
    }

    Text {
        visible: root.error !== ""
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: root.error
        color: Theme.danger
        font.pixelSize: 12
    }

    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Cancel"); onClicked: root.close() },
        BButton {
            variant: "primary"
            enabled: root.items.some(i => i.checked)
            text: qsTr("Add Selected")
            onClicked: {
                const n = App.importDevices(root.tunnelId, root.items.filter(i => i.checked))
                root.close()
                toastTarget.show(n === 1 ? qsTr("1 device added.") : qsTr("%1 devices added.").arg(n))
            }
        }
    ]
    property var toastTarget: null
}
