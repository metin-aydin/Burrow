import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Lists tunnel profiles.
BDialog {
    id: root
    dialogWidth: 560
    title: qsTr("Tunnel Profiles")
    subtitle: qsTr("Devices connect through these profiles. The tunnel runs only inside Burrow; the rest of your computer is not affected.")
    signal confirm(string title, string message, string button, var fn)
    signal editTunnel(string id)
    signal importDevices(string id)

    Repeater {
        model: App.tunnels
        Rectangle {
            Layout.fillWidth: true
            height: 60
            radius: Theme.radius
            color: rowHover.hovered ? Theme.hover : Theme.surfaceSunken
            HoverHandler { id: rowHover }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                spacing: 12
                Rectangle {
                    width: 34; height: 34; radius: 10
                    color: model.type === "tailscale" ? Theme.tsSoft : model.type === "cloudflare" ? Theme.cfSoft : Theme.gpSoft
                    Icon { anchors.centerIn: parent; name: model.type === "tailscale" ? "globe" : model.type === "cloudflare" ? "cloud" : "shield"; size: 17
                           color: model.type === "tailscale" ? Theme.ts : model.type === "cloudflare" ? Theme.cf : Theme.gp }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { text: model.name; color: Theme.text; font.pixelSize: Theme.fontEmphasis; font.weight: Font.Medium }
                    Text {
                        text: (model.type === "tailscale" ? "Tailscale" : model.type === "cloudflare" ? "Cloudflare" : "GlobalProtect") + " · " + model.host
                              + " · " + (model.users === 1 ? qsTr("1 device") : qsTr("%1 devices").arg(model.users))
                        color: Theme.textSecondary; font.pixelSize: Theme.fontLabel
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }
                readonly property var st: App.tunnelStates[model.tunnelId] || {}
                Text {
                    text: {
                        switch (parent.st.state) {
                        case "connected": return qsTr("Connected")
                        case "starting": return qsTr("Connecting…")
                        case "needs_login": return qsTr("Sign-In Needed")
                        case "needs_otp": return qsTr("Code Needed")
                        case "failed": return qsTr("Failed")
                        }
                        return ""
                    }
                    color: parent.st.state === "connected" ? Theme.success
                         : parent.st.state === "failed" ? Theme.danger : Theme.textSecondary
                    font.pixelSize: Theme.fontLabel
                }
                BButton {
                    // Tunnels open by themselves when a device connects; here they can only be closed.
                    visible: !!parent.st.state && parent.st.state !== "failed"
                    text: qsTr("Disconnect")
                    onClicked: App.disconnectTunnel(model.tunnelId)
                }
                BButton {
                    visible: model.type === "tailscale" || model.type === "cloudflare"
                    text: qsTr("Import Devices")
                    onClicked: root.importDevices(model.tunnelId)
                }
                BButton { variant: "ghost"; iconName: "edit"; tip: qsTr("Edit Profile"); onClicked: root.editTunnel(model.tunnelId) }
                BButton {
                    variant: "ghost"; iconName: "trash"; tip: qsTr("Delete Profile")
                    onClicked: {
                        const id = model.tunnelId, n = model.users
                        root.confirm(qsTr("Delete Profile"),
                            n === 1 ? qsTr("\"%1\" is used by 1 device; if deleted, it switches to a direct connection.").arg(model.name)
                          : n > 1 ? qsTr("\"%1\" is used by %2 devices; if deleted, they switch to a direct connection.").arg(model.name).arg(n)
                                  : qsTr("The profile \"%1\" will be deleted.").arg(model.name),
                            qsTr("Delete"), function() { App.deleteTunnel(id) })
                    }
                }
            }
        }
    }
    Text {
        visible: App.tunnels.count === 0
        Layout.fillWidth: true
        text: qsTr("No profiles yet. Add one for your GlobalProtect VPN, Tailscale network or Cloudflare Tunnel.")
        color: Theme.textSecondary
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
    }

    footer: [
        BButton { iconName: "plus"; text: qsTr("New Profile"); onClicked: root.editTunnel("") },
        Item { Layout.fillWidth: true },
        BButton { variant: "primary"; text: qsTr("Done"); onClicked: root.close() }
    ]

}
