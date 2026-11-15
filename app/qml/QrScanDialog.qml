import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtCore
import Burrow.Core

// Scans an authenticator QR code with the camera. Frames are decoded in memory only.
BDialog {
    id: root
    dialogWidth: 480
    title: qsTr("Scan QR Code")
    subtitle: qsTr("Hold the authenticator's export or setup QR code in front of the camera.")
    signal found(string text)
    property string status

    CameraPermission { id: permission }

    onOpened: {
        status = ""
        if (permission.status !== Qt.PermissionStatus.Granted)
            permission.request()
        camera.active = true
        scanTimer.start()
    }
    onClosed: { scanTimer.stop(); camera.active = false }

    CaptureSession {
        camera: Camera { id: camera; active: false }
        videoOutput: preview
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: width * 3 / 4
        radius: Theme.radius
        color: Theme.remoteBackground
        clip: true
        VideoOutput {
            id: preview
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectCrop
        }
        // Viewfinder frame
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * 0.6
            height: width
            color: "transparent"
            radius: 14
            border.color: Theme.accent
            border.width: 3
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0.45; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 900; easing.type: Easing.InOutSine }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: permission.status === Qt.PermissionStatus.Denied
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.onImage
            font.pixelSize: 13
            text: qsTr("Burrow can't use the camera. Allow it in System Settings → Privacy & Security → Camera.")
        }
    }
    Text {
        Layout.fillWidth: true
        visible: root.status !== ""
        text: root.status
        color: Theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    Timer {
        id: scanTimer
        interval: 600
        repeat: true
        onTriggered: preview.grabToImage(function(result) {
            const text = App.decodeQrImage(result.image)
            if (text) {
                root.close()
                root.found(text)
            }
        }, Qt.size(960, 720))
    }

    footer: [
        Item { Layout.fillWidth: true },
        BButton { text: qsTr("Cancel"); onClicked: root.close() }
    ]
}
