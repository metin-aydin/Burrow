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
        title = id ? qsTr("Cihazı Düzenle") : qsTr("Yeni Cihaz")
        subtitle = id ? d.name : qsTr("Bağlanmak istediğin bilgisayarın bilgilerini gir.")
        tunnelCombo.options = App.tunnelChoices().map(t => ({ text: t.name, value: t.id }))
        // Controls lose their bindings once edited, so reset them explicitly.
        nameField.text = d.name || ""; hostField.text = d.host || ""; portField.text = String(d.port || 3389)
        osSeg.value = d.os || "windows"; tunnelCombo.value = d.tunnelId || ""
        userField.text = d.username || ""; domainField.text = d.domain || ""
        savePw.checked = !!d.savePassword; pwField.text = d.password || ""; autoLogin.checked = !!d.autoLogin
        sessionCombo.value = d.sessionMode || "existing"; lockSw.checked = d.lockOnDisconnect !== false
        onDisc.value = d.onDisconnect || "keep_locked"; clipCombo.value = d.clipboard || "both"
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
        options: [{ text: qsTr("Genel"), value: "general" },
                  { text: qsTr("Oturum"), value: "session" },
                  { text: qsTr("Pano Ve Klavye"), value: "input" }]
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
            BField { id: nameField; Layout.fillWidth: true; label: qsTr("Ad"); placeholder: qsTr("İş Bilgisayarı"); text: root.d.name || "" }
            ColumnLayout {
                spacing: 6
                Text { text: qsTr("Sistem"); color: Theme.textSecondary; font.pixelSize: 12; font.weight: Font.Medium }
                Segmented {
                    id: osSeg
                    options: [{ text: "Windows", value: "windows" }, { text: "Ubuntu", value: "ubuntu" }]
                    value: root.d.os || "windows"
                }
            }
        }
        RowLayout {
            spacing: 12
            BField { id: hostField; Layout.fillWidth: true; label: qsTr("Adres"); placeholder: "10.0.0.5"; text: root.d.host || "" }
            BField { id: portField; Layout.preferredWidth: 96; label: qsTr("Port"); text: String(root.d.port || 3389); field.validator: IntValidator { bottom: 1; top: 65535 } }
        }
        BCombo { id: tunnelCombo; Layout.fillWidth: true; label: qsTr("Bağlantı"); value: root.d.tunnelId || "" }
        RowLayout {
            spacing: 12
            BField { id: userField; Layout.fillWidth: true; label: qsTr("Kullanıcı Adı"); placeholder: qsTr("Boş bırakılırsa bağlanırken sorulur"); text: root.d.username || "" }
            BField { id: domainField; Layout.preferredWidth: 160; label: qsTr("Domain"); placeholder: qsTr("Opsiyonel"); text: root.d.domain || "" }
        }
        BSwitch {
            id: savePw
            Layout.fillWidth: true
            title: qsTr("Şifreyi Kasada Sakla")
            description: qsTr("Bilgisayarın şifresi şifreli kasada tutulur.")
            checked: !!root.d.savePassword
        }
        BField {
            id: pwField
            visible: savePw.checked
            Layout.fillWidth: true
            label: qsTr("Şifre")
            password: true
            text: root.d.password || ""
        }
        BSwitch {
            id: autoLogin
            visible: savePw.checked
            Layout.fillWidth: true
            title: qsTr("Otomatik Giriş")
            description: qsTr("Bağlanınca kayıtlı şifreyle giriş yapılır; giriş ekranı çıkarsa şifre oraya da yazılır.")
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
            label: qsTr("Oturum")
            options: [{ text: qsTr("Mevcut oturuma bağlan"), value: "existing" },
                      { text: qsTr("Yeni oturum aç"), value: "new" },
                      { text: qsTr("Her bağlantıda sor"), value: "ask" }]
            value: root.d.sessionMode || "existing"
        }
        BSwitch {
            id: lockSw
            Layout.fillWidth: true
            title: qsTr("Bağlantı Kapanınca Kilitle")
            description: qsTr("Pencereyi kapattığında veya bağlantı koptuğunda uzak bilgisayarın ekranı kilitli kalır.")
            checked: root.d.lockOnDisconnect !== false
        }
        BCombo {
            id: onDisc
            Layout.fillWidth: true
            label: qsTr("Bağlantı Kapanınca")
            options: [{ text: qsTr("Oturumu açık bırak (kilitli)"), value: "keep_locked" },
                      { text: qsTr("Oturumu kapat (sign out)"), value: "sign_out" }]
            value: root.d.onDisconnect || "keep_locked"
        }
    }

    // --- Clipboard & keyboard -------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.tab === "input"
        spacing: 14
        BCombo {
            id: clipCombo
            Layout.fillWidth: true
            label: qsTr("Pano Paylaşımı")
            options: [{ text: qsTr("Çift yönlü"), value: "both" },
                      { text: qsTr("Sadece bu bilgisayardan uzağa"), value: "to_remote" },
                      { text: qsTr("Sadece uzaktan bu bilgisayara"), value: "to_local" },
                      { text: qsTr("Kapalı"), value: "off" }]
            value: root.d.clipboard || "both"
        }
        BCombo {
            id: keyCombo
            Layout.fillWidth: true
            label: qsTr("Mac Kısayolları")
            options: [{ text: qsTr("Kısayolları çevir (Cmd+C → Ctrl+C)"), value: "shortcuts" },
                      { text: qsTr("Cmd ↔ Ctrl tamamen yer değiştir"), value: "swap" },
                      { text: qsTr("Kapalı (tuşlar olduğu gibi)"), value: "off" }]
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
        BButton { text: qsTr("İptal"); onClicked: root.close() },
        BButton { variant: "primary"; text: qsTr("Kaydet"); onClicked: root.save() }
    ]
}
