import QtQuick
import QtQuick.Effects

// Monochrome SVG icon tinted with `color`.
Item {
    id: root
    property string name
    property color color: Theme.text
    property int size: 18
    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        source: root.name ? "icons/" + root.name + ".svg" : ""
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        visible: false
    }
    MultiEffect {
        anchors.fill: img
        source: img
        colorization: 1.0
        colorizationColor: root.color
        brightness: 1.0
    }
}
