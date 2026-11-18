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
    readonly property int ctlH: Theme.contentTitleBar ? Theme.titleBarControlHeight : Theme.controlHeight
    // The narrowest this screen can be with nothing cut off: the header row at its natural width.
    readonly property real minimumContentWidth: header.implicitWidth + header.Layout.leftMargin + header.Layout.rightMargin
    // Development screenshots only (BURROW_DEMO_OPEN=nomatch).
    function demoSearch(t) { search.text = t }
    // Cmd+F (Main.qml).
    function demoAgentInstalled() { agentBar.installing = true; App.agentSetup.install() }   // BURROW_DEMO_OPEN=agent-code
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Header ----------------------------------------------------------
        // On macOS this row is the window's title bar (Theme.titleBarHeight), next to the traffic
        // lights; elsewhere it is a normal header whose left edge matches the chips and cards (28 px).
        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.contentTitleBar ? Theme.titleBarHeight : -1
            Layout.leftMargin: Theme.contentTitleBar ? Theme.trafficLightsInset : 28
            Layout.rightMargin: Theme.contentTitleBar ? Theme.titleBarMargin : 28
            Layout.topMargin: Theme.contentTitleBar ? 0 : 22
            spacing: Theme.contentTitleBar ? 8 : 12
            ColumnLayout {
                visible: !Theme.contentTitleBar
                spacing: 0
                Text { text: "Burrow"; color: Theme.text; font.pixelSize: Theme.fontDisplay; font.weight: Font.Bold }
                Text {
                    text: App.devices.count === 1 ? qsTr("1 device") : qsTr("%1 devices").arg(App.devices.count)
                    color: Theme.textSecondary; font.pixelSize: Theme.fontLabel
                }
            }
            // Title bar: name and count on one line, like a window title.
            Row {
                visible: Theme.contentTitleBar
                spacing: 8
                // Optically centred on the traffic lights (text boxes carry descender space below).
                Layout.topMargin: 4
                Text { text: "Burrow"; color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold; anchors.baseline: count.baseline }
                Text {
                    id: count
                    text: App.devices.count === 1 ? qsTr("1 device") : qsTr("%1 devices").arg(App.devices.count)
                    color: Theme.textSecondary; font.pixelSize: Theme.fontLabel
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
                Layout.preferredHeight: root.ctlH
                radius: Theme.radius
                color: Theme.field
                border.width: search.activeFocus ? 2 : 1
                border.color: search.activeFocus ? Theme.accent : Theme.border
                Icon { id: sIcon; name: "search"; size: 15; color: Theme.textMuted; anchors.left: parent.left; anchors.leftMargin: 11; anchors.verticalCenter: parent.verticalCenter }
                TextField {
                    id: search
                    anchors { left: sIcon.right; right: parent.right; top: parent.top; bottom: parent.bottom; leftMargin: 4 }
                    placeholderText: qsTr("Search devices")
                    Accessible.name: qsTr("Search devices")
                    Keys.onEscapePressed: text = ""
                    placeholderTextColor: Theme.textMuted
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    background: null
                    onTextChanged: App.gallery.query = text
                }
            }
            BButton { iconName: "tunnel"; Layout.preferredHeight: root.ctlH; text: qsTr("Tunnel Profiles"); onClicked: root.openTunnels() }
            BButton { iconName: "lock"; Layout.preferredHeight: root.ctlH; variant: "ghost"; onClicked: App.lock(); tip: qsTr("Lock") }
            BButton { iconName: "settings"; Layout.preferredHeight: root.ctlH; variant: "ghost"; onClicked: root.openSettings(); tip: qsTr("Settings") }
            BButton { iconName: "plus"; Layout.preferredHeight: root.ctlH; variant: "primary"; text: qsTr("New Device"); onClicked: root.newDevice() }
        }

        // --- Tunnel filter chips ---------------------------------------------
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: Theme.contentTitleBar ? 6 : 18
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

        // --- burrow-agent suggestion (Burrow on Ubuntu, ADR 0003) -------------------
        // Inline, not a dialog: it asks for nothing urgent. After a click it shows the pairing code here.
        Rectangle {
            id: agentBar
            property bool showCode: false   // just installed: the code stays until Done
            property bool installing: false
            readonly property var setup: App.agentSetup
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 16
            visible: setup.suggest || installing || showCode
            implicitHeight: barRow.implicitHeight + 28
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.border
            Accessible.role: Accessible.Grouping
            Accessible.name: barTitle.text

            RowLayout {
                id: barRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 18; rightMargin: 14 }
                spacing: 14
                Icon { name: "monitor"; size: 20; color: Theme.accent; Layout.alignment: Qt.AlignTop; Layout.topMargin: 1 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        id: barTitle
                        Layout.fillWidth: true
                        text: agentBar.showCode ? qsTr("This Computer Is Ready To Be Reached")
                                                : qsTr("Reach This Computer's Desktop From Home")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !agentBar.showCode && agentBar.setup.error === ""
                        text: qsTr("Install the Office Screen Helper so Burrow on another computer can open this desktop with its windows as you left them. It unlocks this screen when you connect, keeps the monitor off and locks the screen again when you leave. No administrator password needed.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontLabel
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: agentBar.setup.error !== "" && !agentBar.showCode
                        text: agentBar.setup.error
                        color: Theme.danger
                        font.pixelSize: Theme.fontLabel
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: agentBar.showCode
                        text: qsTr("On your other computer, edit this device in Burrow → Session → Office Screen Helper, and enter this pairing code and port %1. Settings shows the code again.").arg(agentBar.setup.port)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontLabel
                        wrapMode: Text.WordWrap
                    }
                    TextEdit {
                        visible: agentBar.showCode
                        Layout.topMargin: 6
                        text: agentBar.setup.code
                        readOnly: true
                        selectByMouse: true
                        color: Theme.text
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.accentText
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontBody
                        Accessible.name: qsTr("Pairing Code")
                    }
                }
                Spinner { visible: agentBar.setup.busy; Layout.alignment: Qt.AlignVCenter }
                BButton {
                    visible: !agentBar.showCode && !agentBar.setup.busy
                    variant: "ghost"
                    text: qsTr("Not Now")
                    onClicked: agentBar.setup.dismiss()
                }
                BButton {
                    visible: !agentBar.showCode && !agentBar.setup.busy
                    variant: "primary"
                    text: agentBar.setup.error !== "" ? qsTr("Try Again") : qsTr("Install Helper")
                    onClicked: { agentBar.installing = true; agentBar.setup.install() }
                }
                BButton {
                    visible: agentBar.showCode
                    iconName: "copy"
                    text: qsTr("Copy Code")
                    onClicked: agentBar.setup.copyCode()
                }
                BButton {
                    visible: agentBar.showCode
                    variant: "primary"
                    text: qsTr("Done")
                    onClicked: agentBar.showCode = false
                }
            }
            Connections {
                target: App.agentSetup
                function onChanged() {
                    if (!agentBar.installing || App.agentSetup.busy)
                        return
                    // Done: show the code; on an error stay open with it and Try Again.
                    if (App.agentSetup.installed && App.agentSetup.code !== "") {
                        agentBar.installing = false
                        agentBar.showCode = true
                    }
                }
            }
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
            boundsMovement: Flickable.StopAtBounds
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
        // Says what hides the devices (the search, the filter or both) and clears exactly that.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.devices.count > 0 && App.gallery.count === 0
            readonly property bool searching: search.text !== ""
            readonly property bool filtering: root.filterTunnel !== "*"
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 56, 420)
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: parent.parent.searching ? qsTr("No devices match \"%1\".").arg(search.text)
                                                  : qsTr("No devices use this connection.")
                    color: Theme.text
                    font.pixelSize: Theme.fontEmphasis
                    font.weight: Font.Medium
                }
                BButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: parent.parent.searching && parent.parent.filtering ? qsTr("Clear Search And Filter")
                        : parent.parent.searching ? qsTr("Clear Search") : qsTr("Show All Devices")
                    onClicked: { search.text = ""; root.filterTunnel = "*" }
                }
            }
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
                    color: Theme.text; font.pixelSize: Theme.fontDisplay; font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary; font.pixelSize: Theme.fontBody
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
