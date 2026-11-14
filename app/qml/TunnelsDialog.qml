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
                    color: model.type === "tailscale" ? Theme.tsSoft : Theme.gpSoft
                    Icon { anchors.centerIn: parent; name: model.type === "tailscale" ? "globe" : "shield"; size: 17
                           color: model.type === "tailscale" ? Theme.ts : Theme.gp }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { text: model.name; color: Theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                    Text {
                        text: (model.type === "tailscale" ? "Tailscale" : "GlobalProtect") + " · " + model.host
                              + " · " + qsTr("%1 devices").arg(model.users)
                        color: Theme.textSecondary; font.pixelSize: 12
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
                    font.pixelSize: 12
                }
                BButton {
                    text: parent.st.state && parent.st.state !== "failed" ? qsTr("Disconnect") : qsTr("Connect")
                    onClicked: parent.st.state && parent.st.state !== "failed" ? App.disconnectTunnel(model.tunnelId)
                                                                                : App.connectTunnel(model.tunnelId)
                }
                BButton { variant: "ghost"; iconName: "edit"; onClicked: root.editTunnel(model.tunnelId) }
                BButton {
                    variant: "ghost"; iconName: "trash"
                    onClicked: {
                        const id = model.tunnelId, n = model.users
                        root.confirm(qsTr("Delete Profile"),
                            n > 0 ? qsTr("\"%1\" is used by %2 devices; if deleted, they switch to a direct connection.").arg(model.name).arg(n)
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
        text: qsTr("No profiles yet. Add one for your GlobalProtect VPN or Tailscale network.")
        color: Theme.textSecondary
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    footer: [
        BButton { iconName: "plus"; text: qsTr("New Profile"); onClicked: root.editTunnel("") },
        Item { Layout.fillWidth: true },
        BButton { variant: "primary"; text: qsTr("Done"); onClicked: root.close() }
    ]

}
