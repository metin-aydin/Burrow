import QtQuick
import QtQuick.Layouts

// Segmented control over [{text, value}].
Rectangle {
    id: root
    property var options: []
    property var value
    implicitHeight: 34
    implicitWidth: row.implicitWidth + 8
    radius: Theme.radius
    color: Theme.surfaceSunken
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2
        Repeater {
            model: root.options
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitWidth: t.implicitWidth + 28
                radius: Theme.radius - 3
                readonly property bool on: root.value === modelData.value
                color: on ? Theme.surfaceRaised : (m.containsMouse ? Theme.hover : "transparent")
                border.width: on ? 1 : 0
                border.color: Theme.border
                Text {
                    id: t
                    anchors.centerIn: parent
                    text: modelData.text
                    color: parent.on ? Theme.text : Theme.textSecondary
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; onClicked: root.value = modelData.value }
            }
        }
    }
}
