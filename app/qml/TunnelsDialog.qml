import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Lists tunnel profiles.
BDialog {
    id: root
    dialogWidth: 560
    title: qsTr("Tünel Profilleri")
    subtitle: qsTr("Cihazlar bu profiller üzerinden bağlanır. Tünel yalnızca Burrow'un içinde açılır, bilgisayarın geri kalanı etkilenmez.")
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
                              + " · " + qsTr("%1 cihaz").arg(model.users)
                        color: Theme.textSecondary; font.pixelSize: 12
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }
                BButton { variant: "ghost"; iconName: "edit"; onClicked: root.editTunnel(model.tunnelId) }
                BButton {
                    variant: "ghost"; iconName: "trash"
                    onClicked: {
                        const id = model.tunnelId, n = model.users
                        root.confirm(qsTr("Profili Sil"),
                            n > 0 ? qsTr("\"%1\" profilini %2 cihaz kullanıyor; silinirse bu cihazlar doğrudan bağlantıya geçer.").arg(model.name).arg(n)
                                  : qsTr("\"%1\" profili silinecek.").arg(model.name),
                            qsTr("Sil"), function() { App.deleteTunnel(id) })
                    }
                }
            }
        }
    }
    Text {
        visible: App.tunnels.count === 0
        Layout.fillWidth: true
        text: qsTr("Henüz profil yok. GlobalProtect VPN'in veya Tailscale ağın için bir profil ekle.")
        color: Theme.textSecondary
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    footer: [
        BButton { iconName: "plus"; text: qsTr("Yeni Profil"); onClicked: root.editTunnel("") },
        Item { Layout.fillWidth: true },
        BButton { variant: "primary"; text: qsTr("Bitti"); onClicked: root.close() }
    ]

}
