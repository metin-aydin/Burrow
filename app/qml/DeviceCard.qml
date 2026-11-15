import QtQuick
import Burrow.Core
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// Gallery card: last screen preview on top, name / tunnel / address below.
Item {
    id: root
    property string deviceId
    property string name
    property string host
    property string os
    property string tunnelName
    property string tunnelType
    property string tunnelId
    property string preview
    signal connectRequested()
    signal editRequested()
    signal duplicateRequested()
    signal deleteRequested()

    // The menu belongs to the card: keep the hover look while it is open.
    readonly property bool hovered: hover.hovered || menu.visible

    HoverHandler { id: hover }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: root.hovered ? Theme.borderStrong : Theme.border
        scale: root.hovered ? 1.015 : 1
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: 120 } }

        layer.enabled: root.hovered
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.cardShadow
            shadowBlur: 0.8
            shadowVerticalOffset: 6
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 0

            // --- Preview (16:10) -------------------------------------------
            Item {
                id: previewBox
                Layout.fillWidth: true
                Layout.preferredHeight: width * 10 / 16

                Item {
                    id: previewContent
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true

                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.osColors(root.os)[0] }
                            GradientStop { position: 1; color: Theme.osColors(root.os)[1] }
                        }
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            visible: shot.status !== Image.Ready
                            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "monitor"; size: 34; color: Theme.onImageSoft }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Preview After First Connection")
                                color: Theme.onImageMuted
                                font.pixelSize: 11
                            }
                        }
                    }
                    Image {
                        id: shot
                        anchors.fill: parent
                        source: root.preview
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                        sourceSize: Qt.size(width * 2, height * 2)
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                    }
                }
                Rectangle {
                    id: mask
                    anchors.fill: parent
                    radius: Theme.radius
                    visible: false
                    layer.enabled: true
                }
                MultiEffect {
                    anchors.fill: parent
                    source: previewContent
                    maskEnabled: true
                    maskSource: mask
                }

                // OS badge
                Rectangle {
                    anchors { left: parent.left; top: parent.top; margins: 8 }
                    height: 22
                    width: osText.implicitWidth + 16
                    radius: 11
                    color: Theme.imageScrim
                    Text {
                        id: osText
                        anchors.centerIn: parent
                        text: root.os === "ubuntu" ? "Ubuntu" : "Windows"
                        color: Theme.onImage
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                }

                // Menu button
                RoundButton {
                    id: menuBtn
                    anchors { right: parent.right; top: parent.top; margins: 6 }
                    width: 28; height: 28
                    opacity: root.hovered || menu.visible ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    background: Rectangle { radius: 14; color: menuBtn.hovered ? Theme.imageScrimHover : Theme.imageScrim }
                    contentItem: Icon { name: "dots"; size: 16; color: Theme.onImage }
                    onClicked: menu.popup(menuBtn, 0, menuBtn.height + 4)
                }

                TapHandler { onDoubleTapped: root.connectRequested() }
            }

            // --- Info ------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 6
                Layout.rightMargin: 4
                Layout.bottomMargin: 6
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        text: root.name
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 6
                        Chip {
                            text: root.tunnelName !== "" ? root.tunnelName : qsTr("Direct")
                            kind: root.tunnelType
                            status: root.tunnelId ? ((App.tunnelStates[root.tunnelId] || {}).state || "") : ""
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.host
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
                BButton {
                    variant: "primary"
                    text: qsTr("Connect")
                    onClicked: root.connectRequested()
                }
            }
        }
    }

    Menu {
        id: menu
        padding: 4
        background: Rectangle {
            implicitWidth: 180
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.border
        }
        component MenuRow: MenuItem {
            id: mi
            property string iconName
            property bool danger: false
            implicitHeight: 32
            contentItem: Row {
                spacing: 10
                leftPadding: 4
                Icon { name: mi.iconName; size: 15; color: mi.danger ? Theme.danger : Theme.text; anchors.verticalCenter: parent.verticalCenter }
                Text { text: mi.text; color: mi.danger ? Theme.danger : Theme.text; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
            }
            background: Rectangle { radius: 6; color: mi.highlighted ? Theme.hover : "transparent" }
        }
        MenuRow { text: qsTr("Edit"); iconName: "edit"; onTriggered: root.editRequested() }
        MenuRow { text: qsTr("Duplicate"); iconName: "copy"; onTriggered: root.duplicateRequested() }
        MenuRow { text: qsTr("Delete"); iconName: "trash"; danger: true; onTriggered: root.deleteRequested() }
    }
}
