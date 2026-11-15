import QtQuick
import QtQuick.Controls.Basic

// variant: "primary" | "secondary" | "ghost" | "danger"
Button {
    id: control
    property string variant: "secondary"
    property string iconName: ""
    // Short description shown on hover (and keyboard focus), mainly for icon-only buttons.
    property string tip: ""
    implicitHeight: Theme.controlHeight
    leftPadding: text ? 14 : 9
    rightPadding: text ? 14 : 9
    font.pixelSize: Theme.fontBody
    font.weight: Font.Medium
    hoverEnabled: true
    // Screen readers: icon-only buttons are named by their tip; labelled ones keep the tip as detail.
    Accessible.name: text !== "" ? text : tip
    Accessible.description: text !== "" ? tip : ""

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            size: 16
            anchors.verticalCenter: parent.verticalCenter
            color: label.color
        }
        Text {
            id: label
            visible: control.text !== ""
            text: control.text
            font: control.font
            anchors.verticalCenter: parent.verticalCenter
            color: control.variant === "primary" ? Theme.accentText
                 : control.variant === "danger" ? Theme.danger : Theme.text
        }
    }
    }

    background: Rectangle {
        radius: Theme.radius
        color: {
            if (control.variant === "primary")
                return control.down ? Qt.darker(Theme.accent, 1.1)
                     : control.hovered ? Theme.accentHover : Theme.accent
            if (control.variant === "ghost")
                return control.hovered ? Theme.hover : "transparent"
            if (control.variant === "danger")
                return control.hovered ? Theme.dangerSoft : "transparent"
            return control.hovered ? Theme.hover : Theme.surfaceRaised
        }
        border.width: control.variant === "secondary" || control.variant === "danger" ? 1 : 0
        border.color: control.variant === "danger" ? Theme.danger : Theme.border
        opacity: control.enabled ? 1 : 0.5
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    scale: down ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: 80 } }

    BToolTip {
        text: control.tip
        visible: control.tip !== "" && (control.hovered || control.visualFocus) && !control.pressed
    }
}
