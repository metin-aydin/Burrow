import QtQuick
import QtQuick.Effects

// The raised surface behind dropdowns, menus and tooltips: one look everywhere.
Item {
    id: root
    property int radius: Theme.radius
    implicitWidth: 24
    implicitHeight: 24
    RectangularShadow {
        anchors.fill: bg
        offset.y: 4
        blur: 18
        spread: 0
        radius: bg.radius
        color: Theme.popupShadow
    }
    Rectangle {
        id: bg
        anchors.fill: parent
        radius: root.radius
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.borderStrong
    }
}
