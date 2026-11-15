import QtQuick

// Small rounded label. kind: "globalprotect" | "tailscale" | "direct" | "neutral" | "accent"
Rectangle {
    id: root
    property string text
    property string kind: "neutral"
    property bool selected: false
    property string status: ""   // tunnel state: connected | starting | needs_login | needs_otp | failed
    // false: a label only (device cards) — no pointer, no Tab stop, read as text.
    property bool interactive: true
    signal clicked()
    implicitHeight: 24
    implicitWidth: row.implicitWidth + 18
    radius: 12

    readonly property string statusText: {
        switch (status) {
        case "connected": return qsTr("Connected")
        case "starting": return qsTr("Connecting")
        case "needs_login": return qsTr("Sign-In Needed")
        case "needs_otp": return qsTr("Code Needed")
        case "failed": return qsTr("Failed")
        }
        return ""
    }
    activeFocusOnTab: interactive
    Keys.onSpacePressed: root.clicked()
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: statusText !== "" ? text + ", " + statusText : text
    Accessible.checkable: interactive
    Accessible.checked: selected
    Accessible.focusable: interactive
    Accessible.onPressAction: if (interactive) root.clicked()
    // Keyboard focus ring; on the filled (selected) chip it switches to the text colour to stay visible.
    border.width: activeFocus ? 2 : 0
    border.color: selected ? Theme.text : Theme.accent
    color: selected ? Theme.accent
         : kind === "globalprotect" ? Theme.gpSoft
         : kind === "tailscale" ? Theme.tsSoft
         : kind === "cloudflare" ? Theme.cfSoft
         : kind === "accent" ? Theme.accentSoft
         : interactive && mouse.containsMouse ? Theme.hover : Theme.surfaceSunken
    readonly property color fg: selected ? Theme.accentText
         : kind === "globalprotect" ? Theme.gp
         : kind === "tailscale" ? Theme.ts
         : kind === "cloudflare" ? Theme.cf
         : kind === "accent" ? Theme.accent : Theme.textSecondary
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Rectangle {
            visible: root.status !== ""
            width: 7; height: 7; radius: 3.5
            anchors.verticalCenter: parent.verticalCenter
            color: root.status === "connected" ? Theme.success
                 : root.status === "failed" ? Theme.danger : Theme.warning
        }
        Icon {
            visible: root.kind === "globalprotect" || root.kind === "tailscale" || root.kind === "cloudflare"
            name: root.kind === "tailscale" ? "globe" : root.kind === "cloudflare" ? "cloud" : "shield"
            size: 12
            color: root.fg
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            color: root.fg
            font.pixelSize: Theme.fontLabel
            font.weight: Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.interactive
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
