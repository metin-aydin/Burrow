import QtQuick
import QtQuick.Shapes

// Modern activity indicator: a sweeping arc on a faint track.
Item {
    id: root
    property int size: 36
    property real thickness: Math.max(2.5, size / 11)
    property color color: Theme.accent
    property bool running: visible
    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        ShapePath {
            strokeColor: Qt.alpha(root.color, 0.18)
            strokeWidth: root.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.size / 2; centerY: root.size / 2
                radiusX: root.size / 2 - root.thickness; radiusY: radiusX
                startAngle: 0; sweepAngle: 360
            }
        }
    }
    Shape {
        id: arc
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                id: sweep
                centerX: root.size / 2; centerY: root.size / 2
                radiusX: root.size / 2 - root.thickness; radiusY: radiusX
                startAngle: -90
                sweepAngle: 90
            }
        }
        RotationAnimator on rotation {
            from: 0; to: 360; duration: 1100; loops: Animation.Infinite; running: root.running
        }
    }
    // The arc grows and shrinks while it spins.
    SequentialAnimation {
        running: root.running
        loops: Animation.Infinite
        NumberAnimation { target: sweep; property: "sweepAngle"; from: 40; to: 260; duration: 700; easing.type: Easing.InOutCubic }
        NumberAnimation { target: sweep; property: "sweepAngle"; from: 260; to: 40; duration: 700; easing.type: Easing.InOutCubic }
    }
}
