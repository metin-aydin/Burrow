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
    Accessible.role: Accessible.Grouping
    Accessible.name: [root.name, root.tunnelName !== "" ? root.tunnelName : qsTr("Direct"), root.host].join(", ")

    HoverHandler { id: hover }
    // Right-click anywhere on the card: the same Edit / Duplicate / Delete menu as the ••• button.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: menu.popup()
    }

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
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: Theme.osColors(root.os)[0] }
                            GradientStop { position: 1; color: Theme.osColors(root.os)[1] }
                        }
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            // Only when there is no picture: not while one is (re)loading.
                            visible: !root.preview || shot.status === Image.Error
                            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "monitor"; size: 34; color: Theme.onImageSoft }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Preview After First Connection")
                                color: Theme.onImageMuted
                                font.pixelSize: Theme.fontCaption
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
                        // Decoded once at a fixed size: tied to the card size, every resize step reloaded
                        // the picture and it flickered. Keeps the old picture while a new one loads.
                        sourceSize.width: 1280
                        retainWhileLoading: true
                        opacity: status === Image.Ready || status === Image.Loading ? 1 : 0
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
                        text: root.os === "ubuntu" ? "Ubuntu" : root.os === "macos" ? "macOS" : "Windows"
                        color: Theme.onImage
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Medium
                    }
                }

                // Menu button
                RoundButton {
                    id: menuBtn
                    anchors { right: parent.right; top: parent.top; margins: 6 }
                    width: 28; height: 28
                    // Also shown while it has keyboard focus, so Tab can reach it without a pointer.
                    opacity: root.hovered || menu.visible || menuBtn.visualFocus ? 1 : 0
                    Accessible.name: qsTr("Actions For %1").arg(root.name)
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    background: Rectangle {
                        radius: 14
                        color: menuBtn.hovered ? Theme.imageScrimHover : Theme.imageScrim
                        border.width: menuBtn.visualFocus ? 2 : 0
                        border.color: Theme.onImage
                    }
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
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 6
                        Chip {
                            text: root.tunnelName !== "" ? root.tunnelName : qsTr("Direct")
                            interactive: false
                            kind: root.tunnelType
                            status: root.tunnelId ? ((App.tunnelStates[root.tunnelId] || {}).state || "") : ""
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.host
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontLabel
                            elide: Text.ElideRight
                        }
                    }
                }
                BButton {
                    variant: "primary"
                    text: qsTr("Connect")
                    // Four cards, four "Connect" buttons: the spoken name says which device.
                    Accessible.name: qsTr("Connect To %1").arg(root.name)
                    onClicked: root.connectRequested()
                }
            }
        }
    }

    BMenu {
        id: menu
        BMenuItem { text: qsTr("Edit"); iconName: "edit"; onTriggered: root.editRequested() }
        BMenuItem { text: qsTr("Duplicate"); iconName: "copy"; onTriggered: root.duplicateRequested() }
        BMenuItem { text: qsTr("Delete"); iconName: "trash"; danger: true; onTriggered: root.deleteRequested() }
    }
}
