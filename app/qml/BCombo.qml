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

    function openPopup() { combo.popup.open() }   // development screenshots

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
        hoverEnabled: true

        contentItem: Text {
            leftPadding: 12
            rightPadding: 34
            text: combo.displayText
            color: combo.enabled ? Theme.text : Theme.textMuted
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Icon {
            x: combo.width - width - 12
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron"
            size: 14
            color: Theme.textSecondary
            rotation: combo.popup.visible ? -90 : 90
            Behavior on rotation { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
        background: Rectangle {
            radius: Theme.radius
            color: combo.pressed ? Theme.hover : Theme.field
            border.width: combo.visualFocus || combo.popup.visible ? 2 : 1
            border.color: combo.visualFocus || combo.popup.visible ? Theme.accent
                        : combo.hovered ? Theme.borderStrong : Theme.border
            Behavior on border.color { ColorAnimation { duration: 100 } }
        }
        delegate: BMenuItem {
            required property var modelData
            required property int index
            width: ListView.view ? ListView.view.width : combo.width
            text: modelData.text
            chosen: combo.currentIndex === index
            highlighted: combo.highlightedIndex === index
        }
        popup: Popup {
            y: combo.height + 4
            width: combo.width
            padding: 4
            margins: 8
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
            contentItem: ListView {
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: BPopupSurface {}
            enter: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110; easing.type: Easing.OutCubic }
                NumberAnimation { property: "y"; from: combo.height; to: combo.height + 4; duration: 110; easing.type: Easing.OutCubic }
            }
            exit: Transition {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
            }
        }
    }
}
