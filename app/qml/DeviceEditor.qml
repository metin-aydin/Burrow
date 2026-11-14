import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Add / edit a computer (SPEC §4, §4.1).
BDialog {
    id: root
    dialogWidth: 600
    property var d: ({})
    property string error
    property string tab: "general"

    function openFor(id) {
        d = id ? App.device(id) : App.newDevice()
        title = id ? qsTr("Edit Device") : qsTr("New Device")
        subtitle = id ? d.name : qsTr("Enter the details of the computer you want to connect to.")
        tunnelCombo.options = App.tunnelChoices().map(t => ({ text: t.name, value: t.id }))
        // Controls lose their bindings once edited, so reset them explicitly.
        nameField.text = d.name || ""; hostField.text = d.host || ""; portField.text = String(d.port || 3389)
        osSeg.value = d.os || "windows"; tunnelCombo.value = d.tunnelId || ""
        userField.text = d.username || ""; domainField.text = d.domain || ""
        savePw.checked = !!d.savePassword; pwField.text = d.password || ""; autoLogin.checked = !!d.autoLogin
        sessionCombo.value = d.sessionMode || "existing"; lockSw.checked = d.lockOnDisconnect !== false
        onDisc.value = "keep_locked"; clipCombo.value = d.clipboard || "both"
        keyCombo.value = d.keyMap || "shortcuts"
        error = ""
        tab = "general"
        open()
        nameField.field.forceActiveFocus()
    }

    function save() {
        const m = Object.assign({}, d, {
            name: nameField.text, host: hostField.text, port: parseInt(portField.text) || 3389,
            os: osSeg.value, tunnelId: tunnelCombo.value || "",
            username: userField.text, domain: domainField.text,
            savePassword: savePw.checked, password: savePw.checked ? pwField.text : "",
            autoLogin: savePw.checked && autoLogin.checked,
            lockOnDisconnect: lockSw.checked, onDisconnect: onDisc.value,
            sessionMode: sessionCombo.value, clipboard: clipCombo.value, keyMap: keyCombo.value
        })
        const err = App.saveDevice(m)
        if (err) { error = err; tab = "general"; return }
        close()
    }

    Segmented {
        Layout.fillWidth: true
        options: [{ text: qsTr("General"), value: "general" },
                  { text: qsTr("Session"), value: "session" },
                  { text: qsTr("Clipboard And Keyboard"), value: "input" }]
        value: root.tab
        onValueChanged: root.tab = value
    }

    // --- General ---------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.tab === "general"
        spacing: 14
        RowLayout {
            spacing: 12
            BField { id: nameField; Layout.fillWidth: true; label: qsTr("Name"); placeholder: qsTr("Work PC"); text: root.d.name || "" }
            ColumnLayout {
                spacing: 6
                Text { text: qsTr("System"); color: Theme.textSecondary; font.pixelSize: 12; font.weight: Font.Medium }
                Segmented {
                    id: osSeg
                    options: [{ text: "Windows", value: "windows" }, { text: "Ubuntu", value: "ubuntu" }]
                    value: root.d.os || "windows"
                }
            }
        }
        RowLayout {
            spacing: 12
            BField { id: hostField; Layout.fillWidth: true; label: qsTr("Address"); placeholder: "10.0.0.5"; text: root.d.host || "" }
            BField { id: portField; Layout.preferredWidth: 96; label: qsTr("Port"); text: String(root.d.port || 3389); field.validator: IntValidator { bottom: 1; top: 65535 } }
        }
        BCombo { id: tunnelCombo; Layout.fillWidth: true; label: qsTr("Connection"); value: root.d.tunnelId || "" }
        RowLayout {
            spacing: 12
            BField { id: userField; Layout.fillWidth: true; label: qsTr("Username"); placeholder: qsTr("Asked when connecting if left empty"); text: root.d.username || "" }
            BField { id: domainField; Layout.preferredWidth: 160; label: qsTr("Domain"); placeholder: qsTr("Optional"); text: root.d.domain || "" }
        }
        BSwitch {
            id: savePw
            Layout.fillWidth: true
            title: qsTr("Save Password In Vault")
            description: qsTr("The computer's password is kept in the encrypted vault.")
            checked: !!root.d.savePassword
        }
        BField {
            id: pwField
            visible: savePw.checked
            Layout.fillWidth: true
            label: qsTr("Password")
            password: true
            text: root.d.password || ""
        }
        BSwitch {
            id: autoLogin
            visible: savePw.checked
            Layout.fillWidth: true
            title: qsTr("Automatic Sign-In")
            description: qsTr("Signs in with the saved password when connecting; if a login screen appears, the password is typed there too.")
            checked: !!root.d.autoLogin
        }
    }

    // --- Session ---------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.tab === "session"
        spacing: 14
        BCombo {
            id: sessionCombo
            Layout.fillWidth: true
            label: qsTr("Session")
            options: [{ text: qsTr("Connect to the existing session"), value: "existing" },
                      { text: qsTr("Start a new session"), value: "new" },
                      { text: qsTr("Ask every time"), value: "ask" }]
            value: root.d.sessionMode || "existing"
        }
        BSwitch {
            id: lockSw
            Layout.fillWidth: true
            title: qsTr("Lock When Disconnected")
            description: qsTr("Burrow locks the remote computer (Win+L / Super+L) before closing the connection.")
            checked: root.d.lockOnDisconnect !== false
        }
        // "Sign out on disconnect" comes later; until then the session is always kept (locked).
        QtObject { id: onDisc; property string value: "keep_locked" }
    }

    // --- Clipboard & keyboard -------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.tab === "input"
        spacing: 14
        BCombo {
            id: clipCombo
            Layout.fillWidth: true
            label: qsTr("Clipboard Sharing")
            options: [{ text: qsTr("Both directions"), value: "both" },
                      { text: qsTr("Only from this computer to remote"), value: "to_remote" },
                      { text: qsTr("Only from remote to this computer"), value: "to_local" },
                      { text: qsTr("Off"), value: "off" }]
            value: root.d.clipboard || "both"
        }
        BCombo {
            id: keyCombo
            Layout.fillWidth: true
            label: qsTr("Mac Shortcuts")
            options: [{ text: qsTr("Translate shortcuts (Cmd+C → Ctrl+C)"), value: "shortcuts" },
                      { text: qsTr("Swap Cmd ↔ Ctrl completely"), value: "swap" },
                      { text: qsTr("Off (keys as they are)"), value: "off" }]
            value: root.d.keyMap || "shortcuts"
        }
    }

    Text {
        visible: root.error !== ""
        text: root.error
        color: Theme.danger
        font.pixelSize: 12
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }

    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Cancel"); onClicked: root.close() },
        BButton { variant: "primary"; text: qsTr("Save"); onClicked: root.save() }
    ]
}
