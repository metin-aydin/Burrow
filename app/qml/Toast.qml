import QtQuick

Rectangle {
    id: root
    property alias text: label.text
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: shown ? 28 : -height
    property bool shown: false
    width: label.implicitWidth + 36
    height: 40
    radius: 20
    color: Theme.toastBg
    z: 1000
    Behavior on anchors.bottomMargin { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.toastText
        font.pixelSize: Theme.fontBody
    }
    Timer { id: hide; interval: 3200; onTriggered: root.shown = false }
    function show(msg) { text = msg; shown = true; hide.restart() }
}
