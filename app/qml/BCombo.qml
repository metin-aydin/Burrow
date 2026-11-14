import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Labelled combo box over a [{text, value}] model.
ColumnLayout {
    id: root
    property string label
    property var options: []          // [{text, value}]
    property var value
    spacing: 6

    function indexOfValue(v) {
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === v) return i
        return 0
    }

    Text {
        visible: root.label !== ""
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: 12
        font.weight: Font.Medium
    }
    ComboBox {
        id: combo
        Layout.fillWidth: true
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        model: root.options
        textRole: "text"
        currentIndex: root.indexOfValue(root.value)
        onActivated: (i) => root.value = root.options[i].value
        font.pixelSize: 13

        contentItem: Text {
            leftPadding: 12
            text: combo.displayText
            color: Theme.text
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: combo.width - width - 12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: Theme.textSecondary
            font.pixelSize: 14
        }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.field
            border.width: combo.activeFocus ? 2 : 1
            border.color: combo.activeFocus ? Theme.accent : Theme.border
        }
        delegate: ItemDelegate {
            width: combo.width - 8
            x: 4
            height: 32
            highlighted: combo.highlightedIndex === index
            contentItem: Text {
                text: modelData.text
                color: Theme.text
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 6
                color: highlighted ? Theme.hover : "transparent"
            }
        }
        popup: Popup {
            y: combo.height + 4
            width: combo.width
            padding: 4
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
            }
            background: Rectangle {
                radius: Theme.radius
                color: Theme.surfaceRaised
                border.color: Theme.border
            }
        }
    }
}
