import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Add / edit a computer (SPEC §4, §4.1).
BDialog {
    id: root
    dialogWidth: 600
    property var d: ({})
    property string error
    property string tab: "general"
    // Macs are reached over VNC (Screen Sharing, ADR 0008); everything else over RDP.
    readonly property bool isMac: osSeg.value === "macos"
    readonly property bool isWindows: osSeg.value === "windows"
    readonly property bool isUbuntu: osSeg.value === "ubuntu"
    property string folderPath: ""   // the folder picked for sharing (kept while the switch is off)

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
        keyCombo.value = d.keyMap || "shortcuts"; audioCombo.value = d.audio || "local"
        folderSw.checked = !!d.sharedFolder; folderPath = d.sharedFolder || ""; printerSw.checked = !!d.sharePrinters
        helperSw.checked = !!d.officeHelper; agentCodeField.text = d.agentCode || ""; agentPortField.text = String(d.agentPort || 47391)
        displaysCombo.value = d.displays || "window"; retinaSw.checked = !!d.retina
        error = ""
        tab = "general"; tabSeg.value = "general"   // a click breaks the binding, so reset both
        open()
        nameField.field.forceActiveFocus()
    }

    function demoMac() { osSeg.value = "macos" }   // development (--seed-demo, BURROW_DEMO_OPEN=device-mac)
    function demoSharing() { osSeg.value = "windows"; tab = "input"; tabSeg.value = "input"; folderPath = "/Users/demo/Projects"; folderSw.checked = true; printerSw.checked = true }   // BURROW_DEMO_OPEN=device-sharing
    function demoSession() { osSeg.value = "ubuntu"; tab = "session"; tabSeg.value = "session"; helperSw.checked = true; agentCodeField.text = "AAAQ-EAYE-AUDA-OCAJ-BIFQ-YDIO-B4IB-CEQT-CQKR-MFYY-DENB-WHA5-DYPQ" }   // BURROW_DEMO_OPEN=device-session

    function save() {
        const m = Object.assign({}, d, {
            name: nameField.text, host: hostField.text, port: parseInt(portField.text) || 3389,
            os: osSeg.value, protocol: isMac ? "vnc" : "rdp", tunnelId: tunnelCombo.value || "",
            username: userField.text, domain: domainField.text,
            savePassword: savePw.checked, password: savePw.checked ? pwField.text : "",
            autoLogin: savePw.checked && autoLogin.checked,
            lockOnDisconnect: lockSw.checked, onDisconnect: onDisc.value,
            sessionMode: sessionCombo.value, clipboard: clipCombo.value, keyMap: keyCombo.value, audio: audioCombo.value,
            sharedFolder: isWindows && folderSw.checked ? folderPath : "", sharePrinters: isWindows && printerSw.checked,
            displays: displaysCombo.value, retina: retinaSw.checked,
            // The Office Screen Helper exists only for Ubuntu devices (the user's rule); the code stays saved.
            officeHelper: isUbuntu && helperSw.checked, agentCode: agentCodeField.text.trim(),
            agentPort: parseInt(agentPortField.text) || 47391
        })
        if (m.officeHelper && !App.validAgentCode(m.agentCode)) {
            error = qsTr("The pairing code isn't valid. Run burrow-agent install on the Ubuntu computer and copy the code it shows.")
            tab = "session"; tabSeg.value = "session"
            return
        }
        const err = App.saveDevice(m)
        if (err) { error = err; tab = "general"; tabSeg.value = "general"; return }
        close()
    }

    Segmented {
        id: tabSeg
        Layout.fillWidth: true
        // A Mac has only the lock setting of the Session tab; the rest are RDP ones.
        options: root.isMac ? [{ text: qsTr("General"), value: "general" },
                               { text: qsTr("Session"), value: "session" },
                               { text: qsTr("Clipboard"), value: "input" }]
                            : [{ text: qsTr("General"), value: "general" },
                               { text: qsTr("Session"), value: "session" },
                               { text: qsTr("Sharing And Keyboard"), value: "input" }]
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
            BField { id: nameField; Layout.fillWidth: true; Layout.preferredWidth: 300; label: qsTr("Name"); placeholder: qsTr("Work PC"); text: root.d.name || "" }
            ColumnLayout {
                spacing: 6
                Text { text: qsTr("System"); color: Theme.textSecondary; font.pixelSize: Theme.fontLabel; font.weight: Font.Medium }
                Segmented {
                    id: osSeg
                    accessibleName: qsTr("System")
                    options: [{ text: "Windows", value: "windows" }, { text: "Ubuntu", value: "ubuntu" },
                              { text: "macOS", value: "macos" }]
                    value: root.d.os || "windows"
                    // Follow the protocol's usual port unless a custom one was entered.
                    onValueChanged: {
                        if (value === "macos" && portField.text === "3389") portField.text = "5900"
                        else if (value !== "macos" && portField.text === "5900") portField.text = "3389"
                    }
                }
            }
        }
        RowLayout {
            spacing: 12
            BField { id: hostField; Layout.fillWidth: true; Layout.preferredWidth: 300; label: qsTr("Address"); placeholder: "10.0.0.5"; text: root.d.host || "" }
            BField { id: portField; Layout.preferredWidth: 96; label: qsTr("Port"); text: String(root.d.port || 3389); field.validator: IntValidator { bottom: 1; top: 65535 } }
        }
        BCombo { id: tunnelCombo; Layout.fillWidth: true; label: qsTr("Connection"); value: root.d.tunnelId || "" }
        RowLayout {
            spacing: 12
            BField { id: userField; Layout.fillWidth: true; Layout.preferredWidth: 300; label: qsTr("Username"); placeholder: root.isMac ? qsTr("The Mac's user name") : qsTr("Asked when connecting if left empty"); text: root.d.username || "" }
            BField { id: domainField; visible: !root.isMac; Layout.fillWidth: true; Layout.preferredWidth: 150; label: qsTr("Domain"); placeholder: qsTr("Optional"); text: root.d.domain || "" }
        }
        Text {
            Layout.fillWidth: true
            visible: root.isMac
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
            text: qsTr("Turn on Screen Sharing on the Mac (System Settings → General → Sharing) and sign in with the Mac's user name and password.")
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
            visible: savePw.checked && !root.isMac
            Layout.fillWidth: true
            title: qsTr("Automatic Sign-In")
            description: qsTr("Signs in with the saved password when connecting; if a login screen appears, the password is typed there too.")
            checked: !!root.d.autoLogin
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.tab === "general" && !!root.d.certFingerprint
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Burrow trusts this computer's certificate and refuses to connect if it changes.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        BButton {
            text: qsTr("Forget Saved Certificate")
            onClicked: { App.forgetCertificate(root.d.id); root.d = Object.assign({}, root.d, { certFingerprint: "" }) }
        }
    }

    // --- Session ---------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.tab === "session"
        spacing: 14
        BCombo {
            id: sessionCombo
            visible: !root.isMac
            Layout.fillWidth: true
            label: qsTr("Session")
            options: [{ text: qsTr("Connect To The Existing Session"), value: "existing" },
                      { text: qsTr("Start A New Session"), value: "new" },
                      { text: qsTr("Ask Every Time"), value: "ask" }]
            value: root.d.sessionMode || "existing"
        }
        BCombo {
            id: audioCombo
            visible: !root.isMac
            Layout.fillWidth: true
            label: qsTr("Sound")
            options: [{ text: qsTr("Play On This Computer"), value: "local" },
                      { text: qsTr("Play On The Remote Computer"), value: "remote" },
                      { text: qsTr("Off"), value: "off" }]
            value: root.d.audio || "local"
        }
        BSwitch {
            id: lockSw
            Layout.fillWidth: true
            title: qsTr("Lock When Disconnected")
            description: root.isMac ? qsTr("Burrow locks the Mac (Control+Command+Q) before closing the connection.")
                                    : qsTr("Burrow locks the remote computer (Win+L / Super+L) before closing the connection.")
            checked: root.d.lockOnDisconnect !== false
        }
        BCombo {
            id: displaysCombo
            visible: !root.isMac
            Layout.fillWidth: true
            label: qsTr("Displays")
            options: [{ text: qsTr("One Monitor"), value: "window" },
                      { text: qsTr("One Monitor Per Display Of This Computer"), value: "all" }]
            value: root.d.displays || "window"
        }
        Text {
            Layout.fillWidth: true
            visible: displaysCombo.value === "all" && !root.isMac
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
            text: qsTr("Each remote monitor opens as a tab. Drag a tab onto another display to give it its own window; Burrow remembers where you put it. Ubuntu's screen sharing shows only its main monitor; use Remote Login on Ubuntu to get several.")
        }
        BSwitch {
            id: retinaSw
            visible: !root.isMac
            Layout.fillWidth: true
            title: qsTr("Retina Resolution")
            description: qsTr("Sharper, but text looks smaller if the remote computer doesn't scale its display.")
            checked: !!root.d.retina
        }

        // Office Screen Helper (ADR 0003): Ubuntu only, off unless switched on here.
        SectionTitle { visible: root.isUbuntu; text: qsTr("Office Screen") }
        BSwitch {
            id: helperSw
            visible: root.isUbuntu
            Layout.fillWidth: true
            title: qsTr("Office Screen Helper")
            description: qsTr("burrow-agent on the Ubuntu computer unlocks its screen when you connect, keeps its monitor off and locks the screen again when you leave. While you're connected the screen there is unlocked: someone at that desk who moves the mouse sees it for a few seconds.")
        }
        RowLayout {
            visible: root.isUbuntu && helperSw.checked
            Layout.fillWidth: true
            spacing: 10
            BField {
                id: agentCodeField
                Layout.fillWidth: true
                label: qsTr("Pairing Code")
                password: true
                hint: qsTr("Shown by burrow-agent install on the Ubuntu computer.")
            }
            BField {
                id: agentPortField
                Layout.preferredWidth: 110
                label: qsTr("Agent Port")
                field.inputMethodHints: Qt.ImhDigitsOnly
                field.validator: IntValidator { bottom: 1; top: 65535 }
            }
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
            options: [{ text: qsTr("Both Directions"), value: "both" },
                      { text: qsTr("Only From This Computer To Remote"), value: "to_remote" },
                      { text: qsTr("Only From Remote To This Computer"), value: "to_local" },
                      { text: qsTr("Off"), value: "off" }]
            value: root.d.clipboard || "both"
        }
        Text {
            Layout.fillWidth: true
            visible: clipCombo.value !== "off"
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
            text: root.isMac ? qsTr("Over VNC only text is shared: files can't be copied to or from a Mac.")
                             : qsTr("Covers text, images and files. Files from the remote computer are saved on this computer first, up to 2 GB at a time.")
        }
        Text {
            Layout.fillWidth: true
            visible: root.isMac
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
            text: qsTr("Keys reach the Mac as they are: Cmd stays Cmd.")
        }
        BCombo {
            id: keyCombo
            // Only a Mac keyboard has Cmd to translate.
            visible: !root.isMac && Qt.platform.os === "osx"
            Layout.fillWidth: true
            label: qsTr("Mac Shortcuts")
            options: [{ text: qsTr("Translate Shortcuts (Cmd+C → Ctrl+C)"), value: "shortcuts" },
                      { text: qsTr("Swap Cmd ↔ Ctrl Completely"), value: "swap" },
                      { text: qsTr("Off (Keys As They Are)"), value: "off" }]
            value: root.d.keyMap || "shortcuts"
        }

        // Files and printers: Windows only (GNOME's RDP server and a Mac's Screen Sharing don't offer them).
        SectionTitle { visible: root.isWindows; text: qsTr("Files And Printers") }
        BSwitch {
            id: folderSw
            visible: root.isWindows
            Layout.fillWidth: true
            title: qsTr("Share A Folder")
            description: qsTr("The remote computer sees this folder as a drive and can read and change its files. Nothing else on this computer is shared.")
            onToggled: if (checked && root.folderPath === "") folderDialog.open()
        }
        RowLayout {
            visible: root.isWindows && folderSw.checked
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: root.folderPath !== "" ? root.folderPath : qsTr("No folder chosen")
                color: root.folderPath !== "" ? Theme.text : Theme.textSecondary
                font.pixelSize: Theme.fontLabel
                elide: Text.ElideMiddle
            }
            BButton {
                text: qsTr("Choose Folder…")
                onClicked: folderDialog.open()
            }
        }
        BSwitch {
            id: printerSw
            visible: root.isWindows
            Layout.fillWidth: true
            title: qsTr("Share Printers")
            description: qsTr("Documents printed on the remote computer can come out of this computer's printers.")
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Choose A Folder To Share")
        onAccepted: root.folderPath = App.localPath(selectedFolder)
        // Cancelled before any folder was picked: nothing to share.
        onRejected: if (root.folderPath === "") folderSw.checked = false
    }

    Text {
        visible: root.error !== ""
        text: root.error
        color: Theme.danger
        font.pixelSize: Theme.fontLabel
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }

    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Cancel"); onClicked: root.close() },
        BButton { variant: "primary"; text: qsTr("Save"); onClicked: root.save() }
    ]
}
