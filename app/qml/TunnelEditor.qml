import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Add / edit a tunnel profile (SPEC §3).
BDialog {
    id: root
    dialogWidth: 600
    property var t: ({})
    property string error
    property var accounts: []

    function openFor(id) {
        t = id ? App.tunnel(id) : App.newTunnel()
        const gp = t.gp || {}, ts = t.tailscale || {}, totp = gp.totp || {}
        title = id ? qsTr("Edit Tunnel Profile") : qsTr("New Tunnel Profile")
        subtitle = qsTr("The profile name appears on cards and filters.")
        nameField.text = t.name || ""
        typeSeg.value = t.type || "globalprotect"
        gpHost.text = gp.host || ""; gpUser.text = gp.username || ""
        gpSave.checked = !!gp.savePassword; gpPw.text = gp.password || ""
        gpGroup.text = gp.authGroup || ""
        otpOn.checked = !!gp.otpEnabled
        otpMode.value = gp.otpMode || "append"; otpSource.value = gp.otpSource || "totp"
        secret.text = totp.secret || ""; alg.value = totp.algorithm || "SHA1"
        digits.value = totp.digits || 6; period.text = String(totp.period || 30)
        issuer = totp.issuer || ""; account = totp.account || ""
        uri.text = ""
        tsControl.text = ts.controlUrl || ""; tsHost.text = ts.hostname || "burrow"
        error = ""
        open()
        nameField.field.forceActiveFocus()
    }
    property string issuer
    property string account

    function currentTotp() {
        return { secret: secret.text, algorithm: alg.value, digits: digits.value,
                 period: parseInt(period.text) || 30, issuer: issuer, account: account }
    }

    function applyAccount(a) {
        secret.text = a.secret; alg.value = a.algorithm; digits.value = a.digits
        period.text = String(a.period); issuer = a.issuer; account = a.account
        uri.text = ""; accounts = []
    }

    function importUri() {
        const list = App.parseTotp(uri.text)
        if (list.length === 0) { error = qsTr("This is not a valid otpauth link."); return }
        error = ""
        if (list.length === 1) applyAccount(list[0])
        else accounts = list
    }

    function save() {
        const m = Object.assign({}, t, {
            name: nameField.text, type: typeSeg.value,
            gp: { host: gpHost.text, username: gpUser.text, savePassword: gpSave.checked,
                  password: gpSave.checked ? gpPw.text : "", authGroup: gpGroup.text,
                  otpEnabled: otpOn.checked, otpMode: otpMode.value, otpSource: otpSource.value,
                  totp: currentTotp() },
            tailscale: { controlUrl: tsControl.text, hostname: tsHost.text }
        })
        const err = App.saveTunnel(m)
        if (err) { error = err; return }
        close()
    }

    Timer {
        interval: 1000; repeat: true; triggeredOnStart: true
        running: root.visible && otpOn.checked && otpSource.value === "totp"
        onTriggered: {
            const r = App.totpCode(root.currentTotp())
            codeText.text = r.code ? r.code.replace(/(\d{3})(\d+)/, "$1 $2") : (r.error || "—")
            codeText.color = r.error ? Theme.danger : Theme.text
            remain.value = r.remaining ? r.remaining / (parseInt(period.text) || 30) : 0
        }
    }

    RowLayout {
        spacing: 12
        BField { id: nameField; Layout.fillWidth: true; label: qsTr("Profile Name"); placeholder: qsTr("Work VPN") }
        ColumnLayout {
            spacing: 6
            Text { text: qsTr("Type"); color: Theme.textSecondary; font.pixelSize: 12; font.weight: Font.Medium }
            Segmented {
                id: typeSeg
                options: [{ text: "GlobalProtect", value: "globalprotect" }, { text: "Tailscale", value: "tailscale" }]
            }
        }
    }

    // --- GlobalProtect ----------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: typeSeg.value === "globalprotect"
        spacing: 14
        RowLayout {
            spacing: 12
            BField { id: gpHost; Layout.fillWidth: true; label: qsTr("Server"); placeholder: "vpn.sirket.com" }
            BField { id: gpGroup; Layout.preferredWidth: 170; label: qsTr("Gateway / Group"); placeholder: qsTr("Optional") }
        }
        BField { id: gpUser; Layout.fillWidth: true; label: qsTr("Username") }
        BSwitch { id: gpSave; Layout.fillWidth: true; title: qsTr("Save Password In Vault"); description: qsTr("If not saved, it's asked on every connection.") }
        BField { id: gpPw; visible: gpSave.checked; Layout.fillWidth: true; label: qsTr("Password"); password: true }

        SectionTitle { text: qsTr("One-Time Password (OTP)") }
        BSwitch { id: otpOn; Layout.fillWidth: true; title: qsTr("Use OTP"); description: qsTr("An authenticator code is required in addition to the password.") }
        ColumnLayout {
            Layout.fillWidth: true
            visible: otpOn.checked
            spacing: 14
            RowLayout {
                spacing: 12
                BCombo { id: otpMode; Layout.fillWidth: true; label: qsTr("OTP Type")
                    options: [{ text: qsTr("Append to password"), value: "append" }, { text: qsTr("Separate prompt (challenge)"), value: "separate" }] }
                BCombo { id: otpSource; Layout.fillWidth: true; label: qsTr("OTP Source")
                    options: [{ text: qsTr("Automatic (authenticator key)"), value: "totp" }, { text: qsTr("Enter manually each time"), value: "manual" }] }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: otpSource.value === "totp"
                spacing: 14
                RowLayout {
                    spacing: 8
                    BField { id: uri; Layout.fillWidth: true; label: qsTr("Authenticator Link"); placeholder: "otpauth://… veya otpauth-migration://…"; onAccepted: root.importUri() }
                    BButton { Layout.alignment: Qt.AlignBottom; text: qsTr("Import"); onClicked: root.importUri() }
                }
                // Several accounts in a migration QR: pick one.
                Flow {
                    Layout.fillWidth: true
                    visible: root.accounts.length > 0
                    spacing: 6
                    Repeater {
                        model: root.accounts
                        Chip { kind: "accent"; text: (modelData.issuer ? modelData.issuer + " — " : "") + modelData.account; onClicked: root.applyAccount(modelData) }
                    }
                }
                RowLayout {
                    spacing: 12
                    BField { id: secret; Layout.fillWidth: true; label: qsTr("Key"); password: true; placeholder: qsTr("Base32") }
                    BCombo { id: alg; Layout.preferredWidth: 110; label: qsTr("Algorithm")
                        options: [{ text: "SHA1", value: "SHA1" }, { text: "SHA256", value: "SHA256" }, { text: "SHA512", value: "SHA512" }] }
                    BCombo { id: digits; Layout.preferredWidth: 80; label: qsTr("Digits")
                        options: [{ text: "6", value: 6 }, { text: "8", value: 8 }] }
                    BField { id: period; Layout.preferredWidth: 70; label: qsTr("Period (Sec)"); field.validator: IntValidator { bottom: 10; top: 300 } }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 64
                    radius: Theme.radius
                    color: Theme.surfaceSunken
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 14
                        ColumnLayout {
                            spacing: 2
                            Text { text: qsTr("Current Code"); color: Theme.textSecondary; font.pixelSize: 11 }
                            Text { id: codeText; text: "—"; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold; font.family: "Menlo" }
                        }
                        ProgressBar {
                            id: remain
                            Layout.fillWidth: true
                            from: 0; to: 1
                            background: Rectangle { implicitHeight: 4; radius: 2; color: Theme.border }
                            contentItem: Item { Rectangle { width: remain.visualPosition * parent.width; height: 4; radius: 2; color: Theme.accent } }
                        }
                        Text {
                            text: qsTr("Should match the code on your phone")
                            color: Theme.textMuted; font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }

    // --- Tailscale ---------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: typeSeg.value === "tailscale"
        spacing: 14
        BField { id: tsControl; Layout.fillWidth: true; label: qsTr("Control Server"); placeholder: qsTr("Tailscale if empty (enter a URL for Headscale)") }
        BField { id: tsHost; Layout.fillWidth: true; label: qsTr("Node Name"); hint: qsTr("The Tailscale sign-in page opens on first connection.") }
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
