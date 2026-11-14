import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A settings row: title + description on the left, switch on the right.
RowLayout {
    id: root
    property string title
    property string description
    property alias checked: sw.checked
    signal toggled()
    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text { text: root.title; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
        Text {
            visible: root.description !== ""
            text: root.description
            color: Theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    Switch {
        id: sw
        onToggled: root.toggled()
        opacity: enabled ? 1 : 0.5
        indicator: Rectangle {
            implicitWidth: 38
            implicitHeight: 22
            radius: 11
            color: sw.checked ? Theme.accent : Theme.borderStrong
            Behavior on color { ColorAnimation { duration: 120 } }
            Rectangle {
                x: sw.checked ? parent.width - width - 3 : 3
                y: 3
                width: 16; height: 16; radius: 8
                color: "white"
                Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            }
        }
    }
}
