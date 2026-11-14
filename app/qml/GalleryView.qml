import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Main screen: device cards with last-screen previews.
Rectangle {
    id: root
    color: Theme.bg
    property alias filterTunnel: filterHolder.tunnel
    QtObject { id: filterHolder; property string tunnel: App.gallery.tunnel; onTunnelChanged: App.gallery.tunnel = tunnel }

    signal editDevice(string id)
    signal newDevice()
    signal openTunnels()
    signal openSettings()
    signal confirmDelete(string id, string name)
    signal connectDevice(string id)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Header ----------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 22
            spacing: 12
            Image {
                Layout.preferredWidth: 42; Layout.preferredHeight: 42
                source: "../resources/icons/256x256.png"
                sourceSize: Qt.size(84, 84)
            }
            ColumnLayout {
                spacing: 0
                Text { text: "Burrow"; color: Theme.text; font.pixelSize: 20; font.weight: Font.Bold }
                Text {
                    text: App.devices.count === 1 ? qsTr("1 device") : qsTr("%1 devices").arg(App.devices.count)
                    color: Theme.textSecondary; font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }
            Chip {
                visible: ["available", "downloading", "ready"].indexOf(App.updater.state) >= 0
                kind: "accent"
                text: qsTr("Update %1").arg(App.updater.latestVersion)
                onClicked: root.openSettings()
            }
            // Search
            Rectangle {
                Layout.preferredWidth: 240
                height: Theme.controlHeight
                radius: Theme.radius
                color: Theme.field
                border.width: search.activeFocus ? 2 : 1
                border.color: search.activeFocus ? Theme.accent : Theme.border
                Icon { id: sIcon; name: "search"; size: 15; color: Theme.textMuted; anchors.left: parent.left; anchors.leftMargin: 11; anchors.verticalCenter: parent.verticalCenter }
                TextField {
                    id: search
                    anchors { left: sIcon.right; right: parent.right; top: parent.top; bottom: parent.bottom; leftMargin: 4 }
                    placeholderText: qsTr("Search devices")
                    placeholderTextColor: Theme.textMuted
                    color: Theme.text
                    font.pixelSize: 13
                    background: null
                    onTextChanged: App.gallery.query = text
                }
            }
            BButton { iconName: "tunnel"; text: qsTr("Tunnel Profiles"); onClicked: root.openTunnels() }
            BButton { iconName: "lock"; variant: "ghost"; onClicked: App.lock(); ToolTip.visible: hovered; ToolTip.text: qsTr("Lock") }
            BButton { iconName: "settings"; variant: "ghost"; onClicked: root.openSettings(); ToolTip.visible: hovered; ToolTip.text: qsTr("Settings") }
            BButton { iconName: "plus"; variant: "primary"; text: qsTr("New Device"); onClicked: root.newDevice() }
        }

        // --- Tunnel filter chips ---------------------------------------------
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 18
            spacing: 8
            visible: App.tunnels.count > 0
            Chip { text: qsTr("All"); selected: root.filterTunnel === "*"; onClicked: root.filterTunnel = "*" }
            Repeater {
                model: App.tunnels
                Chip {
                    text: model.name
                    kind: model.type
                    status: (App.tunnelStates[model.tunnelId] || {}).state || ""
                    selected: root.filterTunnel === model.tunnelId
                    onClicked: root.filterTunnel = model.tunnelId
                }
            }
            Chip { text: qsTr("Direct"); selected: root.filterTunnel === ""; onClicked: root.filterTunnel = "" }
        }

        // --- Grid ------------------------------------------------------------
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 14
            leftMargin: 20
            rightMargin: 20
            bottomMargin: 24
            clip: true
            visible: App.gallery.count > 0
            readonly property int columns: Math.max(1, Math.floor((width - 40) / 300))
            cellWidth: (width - 40) / columns
            cellHeight: cellWidth * 10 / 16 + 96
            model: App.gallery
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                required property var model
                width: grid.cellWidth
                height: grid.cellHeight
                DeviceCard {
                    anchors.fill: parent
                    anchors.margins: 8
                    deviceId: model.deviceId
                    name: model.name
                    host: model.host
                    os: model.os
                    tunnelName: model.tunnelName
                    tunnelType: model.tunnelType
                    tunnelId: model.tunnelId
                    preview: model.preview
                    onConnectRequested: root.connectDevice(model.deviceId)
                    onEditRequested: root.editDevice(model.deviceId)
                    onDuplicateRequested: App.duplicateDevice(model.deviceId)
                    onDeleteRequested: root.confirmDelete(model.deviceId, model.name)
                }
            }
        }

        // --- No search results -----------------------------------------------
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.devices.count > 0 && App.gallery.count === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("No matching devices.")
            color: Theme.textSecondary
            font.pixelSize: 14
        }

        // --- Empty state -----------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.devices.count === 0
            ColumnLayout {
                anchors.centerIn: parent
                width: 420
                spacing: 14
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 72; height: 72; radius: 22
                    color: Theme.accentSoft
                    Icon { anchors.centerIn: parent; name: "monitor"; size: 34; color: Theme.accent }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Add Your First Computer")
                    color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary; font.pixelSize: 13
                    text: qsTr("Add the computer you want to reach. If you connect through a VPN or Tailscale, create a tunnel profile first.")
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    BButton { iconName: "tunnel"; text: qsTr("Add Tunnel Profile"); onClicked: root.openTunnels() }
                    BButton { iconName: "plus"; variant: "primary"; text: qsTr("New Device"); onClicked: root.newDevice() }
                }
            }
        }
    }
}
