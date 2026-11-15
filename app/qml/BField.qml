import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Labelled text field.
ColumnLayout {
    id: root
    property string label
    property string hint
    property alias text: field.text
    property alias placeholder: field.placeholderText
    property bool password: false
    // Whether a password field currently shows its text; hidden again whenever the field is hidden.
    property bool revealed: false
    property alias field: field
    signal accepted()
    spacing: 6
    onVisibleChanged: if (!visible) revealed = false
    onPasswordChanged: revealed = false

    Text {
        visible: root.label !== ""
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
        font.weight: Font.Medium
    }
    TextField {
        id: field
        Layout.fillWidth: true
        // Fixed implicit width: the layout decides the width, so typing never resizes the field.
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        Accessible.name: root.label !== "" ? root.label : placeholderText
        Accessible.description: root.hint
        Accessible.passwordEdit: root.password && !root.revealed
        echoMode: root.password && !root.revealed ? TextInput.Password : TextInput.Normal
        color: Theme.text
        placeholderTextColor: Theme.textMuted
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: Theme.fontBody
        leftPadding: 12
        rightPadding: root.password ? revealBtn.width + 8 : 12
        onAccepted: root.accepted()

        // Show / hide toggle for secret fields. Never takes focus, so the caret stays in the field
        // and Tab still moves field to field.
        BButton {
            id: revealBtn
            visible: root.password
            variant: "ghost"
            iconName: root.revealed ? "eye-off" : "eye"
            tip: root.revealed ? qsTr("Hide Password") : qsTr("Show Password")
            focusPolicy: Qt.NoFocus
            implicitHeight: parent.height - 8
            leftPadding: 7
            rightPadding: 7
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.revealed = !root.revealed
        }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.field
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? Theme.accent : Theme.border
        }
    }
    Text {
        visible: root.hint !== ""
        text: root.hint
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
