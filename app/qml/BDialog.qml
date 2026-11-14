import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Centered modal card with a title and a footer. Put content in `body`.
Popup {
    id: root
    property string title
    property string subtitle
    default property alias body: content.data
    property alias footer: footerRow.data
    property int dialogWidth: 520

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(dialogWidth, parent ? parent.width - 48 : dialogWidth)
    height: Math.min(implicitHeight, parent ? parent.height - 48 : implicitHeight)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#55141820" }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 160; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.bottomMargin: 8
            spacing: 4
            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.subtitle !== ""
                text: root.subtitle
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: content.implicitHeight + 16
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: content
                width: parent.width - 48
                x: 24
                y: 8
                spacing: 14
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        RowLayout {
            id: footerRow
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 8
        }
    }
}
