import QtQuick
import QtQuick.Controls.Basic

// A row in a BMenu: optional icon, a check for the chosen entry, red for destructive actions.
MenuItem {
    id: mi
    property string iconName
    property bool danger: false
    property bool chosen: false      // shows a check at the end (current choice)
    implicitHeight: 32
    implicitWidth: Math.max(180, row.implicitWidth + 24)
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: Theme.fontBody

    readonly property color tint: !mi.enabled ? Theme.textMuted : mi.danger ? Theme.danger : Theme.text
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Icon { visible: mi.iconName !== ""; name: mi.iconName; size: 15; color: mi.tint; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: mi.text
                color: mi.tint
                font.pixelSize: mi.font.pixelSize
                font.weight: mi.chosen ? Font.Medium : Font.Normal
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Icon {
            visible: mi.chosen
            name: "check"
            size: 14
            color: Theme.accent
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        }
    }
    background: Rectangle {
        radius: 6
        color: !mi.enabled ? "transparent"
             : mi.pressed ? Theme.borderStrong
             : mi.highlighted ? (mi.danger ? Theme.dangerSoft : Theme.hover) : "transparent"
    }
}
