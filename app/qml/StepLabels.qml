import QtQuick

// Translatable labels for Steps.js.
QtObject {
    readonly property string gpConnect: qsTr("Reaching the VPN server")
    readonly property string gpPortal: qsTr("Signing in to the portal")
    readonly property string gpGateway: qsTr("Signing in to the gateway")
    readonly property string gpTunnel: qsTr("Opening the tunnel")
    readonly property string tsStart: qsTr("Starting the Tailscale node")
    readonly property string tsLogin: qsTr("Joining your tailnet")
    readonly property string cfApp: qsTr("Finding the Access application")
    readonly property string cfLogin: qsTr("Signing in to Cloudflare Access")
    readonly property string rdpConnect: qsTr("Connecting to %1")
    readonly property string rdpCert: qsTr("Checking the certificate")
    readonly property string rdpSignIn: qsTr("Signing in")
    readonly property string rdpOpen: qsTr("Opening the desktop")
}
