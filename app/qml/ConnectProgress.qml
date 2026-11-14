import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Burrow.Core
import "Steps.js" as Steps

// Shows the tunnel's progress while a device waits for it; the session window takes over after.
BDialog {
    id: root
    dialogWidth: 440
    property string deviceId
    property var tunnel: ({})
    property string error
    readonly property var st: App.tunnelStates[tunnel.id] || {}
    signal retry(string deviceId)
    StepLabels { id: labels }

    function show(devId) {
        deviceId = devId
        tunnel = App.deviceTunnel(devId)
        error = ""
        title = qsTr("Connecting %1").arg(App.device(devId).name || "")
        subtitle = qsTr("Through %1").arg(tunnel.name || "")
        open()
    }
    function fail(devId, message) {
        if (devId !== deviceId || !visible) return
        error = message || qsTr("The tunnel couldn't be opened.")
    }

    ConnectionSteps {
        Layout.fillWidth: true
        steps: Steps.tunnelSteps(root.tunnel.type || "", labels)
        current: root.error ? (Steps.tunnelCurrent(root.st) || root.steps0()) : Steps.tunnelCurrent(root.st)
        failed: root.error !== ""
        wait: root.st.wait || 0
        detail: root.st.state === "needs_login" ? qsTr("Finish signing in in your browser.") : ""
    }
    function steps0() { return Steps.tunnelSteps(tunnel.type || "", labels)[0].id }
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
        BButton { text: root.error ? qsTr("Close") : qsTr("Cancel"); onClicked: root.close() },
        BButton { visible: root.error !== ""; variant: "primary"; text: qsTr("Try Again"); onClicked: { root.close(); root.retry(root.deviceId) } }
    ]
}
