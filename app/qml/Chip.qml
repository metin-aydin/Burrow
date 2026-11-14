import QtQuick

// Small rounded label. kind: "globalprotect" | "tailscale" | "direct" | "neutral" | "accent"
Rectangle {
    id: root
    property string text
    property string kind: "neutral"
    property bool selected: false
    signal clicked()
    implicitHeight: 24
    implicitWidth: row.implicitWidth + 18
    radius: 12
    color: selected ? Theme.accent
         : kind === "globalprotect" ? Theme.gpSoft
         : kind === "tailscale" ? Theme.tsSoft
         : kind === "accent" ? Theme.accentSoft
         : mouse.containsMouse ? Theme.hover : Theme.surfaceSunken
    readonly property color fg: selected ? Theme.accentText
         : kind === "globalprotect" ? Theme.gp
         : kind === "tailscale" ? Theme.ts
         : kind === "accent" ? Theme.accent : Theme.textSecondary
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Icon {
            visible: root.kind === "globalprotect" || root.kind === "tailscale"
            name: root.kind === "tailscale" ? "globe" : "shield"
            size: 12
            color: root.fg
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            color: root.fg
            font.pixelSize: 12
            font.weight: Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
