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
    property alias field: field
    signal accepted()
    spacing: 6

    Text {
        visible: root.label !== ""
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: 12
        font.weight: Font.Medium
    }
    TextField {
        id: field
        Layout.fillWidth: true
        // Fixed implicit width: the layout decides the width, so typing never resizes the field.
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        echoMode: root.password ? TextInput.Password : TextInput.Normal
        color: Theme.text
        placeholderTextColor: Theme.textMuted
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        onAccepted: root.accepted()
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
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
