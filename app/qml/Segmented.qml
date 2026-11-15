import QtQuick
import QtQuick.Layouts

// Segmented control over [{text, value}]; every segment has the same width.
Rectangle {
    id: root
    property var options: []
    property var value
    readonly property real segmentWidth: {
        let w = 0
        for (let i = 0; i < measure.count; ++i) {
            const item = measure.itemAt(i)
            if (item) w = Math.max(w, item.implicitWidth)
        }
        return w + 28
    }
    implicitHeight: 34
    implicitWidth: options.length * segmentWidth + (options.length - 1) * row.spacing + 6
    radius: Theme.radius
    color: Theme.surfaceSunken
    // Keyboard: one Tab stop, arrows move the choice (like a native segmented control).
    property string accessibleName: ""
    activeFocusOnTab: true
    border.width: activeFocus ? 2 : 0
    border.color: Theme.accent
    Accessible.role: Accessible.Grouping
    Accessible.name: accessibleName
    function step(d) {
        let i = options.findIndex(o => o.value === value)
        i = Math.max(0, Math.min(options.length - 1, i + d))
        if (options.length) value = options[i].value
    }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)

    // Invisible copies of the labels, only to measure the widest one.
    Repeater {
        id: measure
        model: root.options
        Text { visible: false; text: modelData.text; font.pixelSize: Theme.fontBody; font.weight: Font.Medium }
    }

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
                Layout.preferredWidth: root.segmentWidth   // equal preferred widths → equal segments
                radius: Theme.radius - 3
                readonly property bool on: root.value === modelData.value
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.text
                Accessible.checkable: true
                Accessible.checked: on
                Accessible.onPressAction: root.value = modelData.value
                color: on ? Theme.surfaceRaised : (m.containsMouse ? Theme.hover : "transparent")
                border.width: on ? 1 : 0
                border.color: Theme.border
                Behavior on color { ColorAnimation { duration: 120 } }
                Text {
                    anchors.centerIn: parent
                    text: modelData.text
                    color: parent.on ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                }
                MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; onClicked: root.value = modelData.value }
            }
        }
    }
}
