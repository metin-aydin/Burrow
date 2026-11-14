import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

// First run (setup) or unlock screen.
Rectangle {
    id: root
    color: Theme.bg
    readonly property bool setup: App.state === "setup"
    property string error
    property int failures: 0

    function submit() {
        if (!submitBtn.enabled) return
        error = ""
        busy.running = true
        derive.start()
    }

    // Short delay so the button text repaints before the blocking key derivation.
    Timer {
        id: derive
        interval: 30
        onTriggered: {
            const err = root.setup ? App.createVault(pw.text, pw2.text) : App.unlock(pw.text)
            busy.running = false
            pw.text = ""; pw2.text = ""
            if (err) {
                root.error = err
                if (!root.setup) {
                    submitBtn.enabled = false
                    cooldown.interval = Math.min(30000, 1000 * Math.pow(2, Math.min(root.failures++, 5)))
                    cooldown.start()
                }
                pw.field.forceActiveFocus()
            }
        }
    }

    Timer { id: cooldown; onTriggered: submitBtn.enabled = true }

    Component.onCompleted: {
        pw.field.forceActiveFocus()
        if (!setup && App.touchIdEnabled && App.touchIdAvailable)
            Qt.callLater(function() { const e = App.unlockWithTouchId(); if (e) root.error = e })
    }

    Rectangle {
        anchors.centerIn: parent
        width: 400
        height: col.implicitHeight + 64
        radius: 20
        color: Theme.surface
        border.color: Theme.border

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 32
            spacing: 16

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 64; height: 64; radius: 18
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0; color: Theme.accent }
                    GradientStop { position: 1; color: Qt.darker(Theme.accent, 1.5) }
                }
                Icon { anchors.centerIn: parent; name: "tunnel"; size: 32; color: "white" }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.setup ? qsTr("Welcome To Burrow") : qsTr("Burrow Is Locked")
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: root.setup
                    ? qsTr("Your devices, passwords and OTP keys are encrypted with this master password and stored only on this computer. If you forget it, the data cannot be recovered.")
                    : qsTr("Enter your master password to continue.")
            }
            BField {
                id: pw
                Layout.fillWidth: true
                label: qsTr("Master Password")
                password: true
                onAccepted: root.setup ? pw2.field.forceActiveFocus() : root.submit()
            }
            BField {
                id: pw2
                visible: root.setup
                Layout.fillWidth: true
                label: qsTr("Master Password (Again)")
                password: true
                hint: qsTr("At least 8 characters.")
                onAccepted: root.submit()
            }
            Text {
                visible: root.error !== ""
                text: root.error
                color: Theme.danger
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            BButton {
                id: submitBtn
                Layout.fillWidth: true
                variant: "primary"
                text: busy.running ? qsTr("Deriving Key…")
                    : root.setup ? qsTr("Create Vault") : qsTr("Unlock")
                onClicked: root.submit()
            }
            BButton {
                visible: !root.setup && App.touchIdEnabled && App.touchIdAvailable
                Layout.fillWidth: true
                variant: "ghost"
                iconName: "fingerprint"
                text: qsTr("Unlock With Touch ID")
                onClicked: { const e = App.unlockWithTouchId(); if (e) root.error = e }
            }
            BusyIndicator { id: busy; running: false; visible: false }
        }
    }
}
