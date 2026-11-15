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
    // Never taller than the window (with a margin); the body scrolls instead.
    height: Math.min(implicitHeight, parent ? parent.height - 80 : implicitHeight)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: Theme.modalScrim }

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
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.subtitle !== ""
                text: root.subtitle
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 8 px above the content and 24 px below it, so the last field never touches the footer.
            Layout.preferredHeight: content.implicitHeight + 32
            Layout.minimumHeight: 60
        ScrollView {
            id: scroller
            anchors.fill: parent
            contentWidth: availableWidth
            contentHeight: content.implicitHeight + 32
            clip: true
            // Plain scrolling: no rubber-band bounce at either end (wheel or trackpad).
            Component.onCompleted: {
                contentItem.boundsBehavior = Flickable.StopAtBounds
                contentItem.boundsMovement = Flickable.StopAtBounds
            }
            ColumnLayout {
                id: content
                width: parent.width - 48
                x: 24
                y: 8
                spacing: 14
            }
        }
        // Scroll cues when the body is taller than the dialog: a hairline under the title once the
        // content has moved up, and a fade into the footer while more lies below. Without them a field
        // cut by the footer looks like the end of the form.
        readonly property Flickable flick: scroller.contentItem
        readonly property bool scrollable: flick && flick.contentHeight > flick.height + 1
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: Theme.border
            visible: parent.scrollable && !parent.flick.atYBeginning
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 36
            visible: parent.scrollable && !parent.flick.atYEnd
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0) }
                GradientStop { position: 1; color: Theme.surface }
            }
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
