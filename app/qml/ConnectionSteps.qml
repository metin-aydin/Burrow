import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes

// Animated step list for connection progress. `steps` is [{id, label}]; `current` is the active step
// id ("" = not started, "done" = all finished); `failed` marks the active step as failed; `wait` shows
// a countdown on the active step (seconds) and `waitTotal` its full length.
ColumnLayout {
    id: root
    property var steps: []
    property string current: ""
    property bool failed: false
    property int wait: 0
    property int waitTotal: 30
    property string detail: ""
    spacing: 4

    function indexOf(id) {
        if (id === "done") return steps.length
        for (let i = 0; i < steps.length; ++i)
            if (steps[i].id === id) return i
        return -1
    }
    readonly property int activeIndex: indexOf(current)

    Repeater {
        model: root.steps
        RowLayout {
            id: row
            required property var modelData
            required property int index
            readonly property bool done: index < root.activeIndex
            readonly property bool active: index === root.activeIndex
            Layout.fillWidth: true
            spacing: 12
            opacity: done || active ? 1 : 0.45
            Behavior on opacity { NumberAnimation { duration: 250 } }

            Item {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                // Done: a filled circle with a check that pops in.
                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    color: Theme.success
                    scale: row.done ? 1 : 0.4
                    opacity: row.done ? 1 : 0
                    Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                    Icon { anchors.centerIn: parent; name: "check"; size: 15; color: "white" }
                }
                // Active: spinner, or a countdown ring while waiting.
                Spinner {
                    anchors.fill: parent
                    size: 26
                    visible: row.active && !root.failed && root.wait <= 0
                    running: visible
                }
                Shape {
                    anchors.fill: parent
                    visible: row.active && !root.failed && root.wait > 0
                    layer.enabled: true
                    layer.samples: 4
                    ShapePath {
                        strokeColor: Qt.alpha(Theme.warning, 0.25); strokeWidth: 3; fillColor: "transparent"
                        PathAngleArc { centerX: 13; centerY: 13; radiusX: 11; radiusY: 11; startAngle: 0; sweepAngle: 360 }
                    }
                    ShapePath {
                        strokeColor: Theme.warning; strokeWidth: 3; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                        PathAngleArc {
                            centerX: 13; centerY: 13; radiusX: 11; radiusY: 11; startAngle: -90
                            sweepAngle: 360 * Math.max(0, Math.min(1, root.wait / Math.max(1, root.waitTotal)))
                            Behavior on sweepAngle { NumberAnimation { duration: 900 } }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: row.active && !root.failed && root.wait > 0
                    text: root.wait
                    color: Theme.warning
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
                // Failed: red circle with a cross.
                Rectangle {
                    anchors.fill: parent
                    radius: 13
                    visible: row.active && root.failed
                    color: Theme.danger
                    Icon { anchors.centerIn: parent; name: "x"; size: 14; color: "white" }
                }
                // Pending: hollow circle.
                Rectangle {
                    anchors.centerIn: parent
                    width: 12; height: 12; radius: 6
                    visible: !row.done && !row.active
                    color: "transparent"
                    border.color: Theme.borderStrong
                    border.width: 2
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: row.modelData.label
                    color: row.active && root.failed ? Theme.danger : Theme.text
                    font.pixelSize: 13
                    font.weight: row.active ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: row.active && text !== ""
                    text: root.wait > 0 ? qsTr("Waiting for the next one-time code… %1 s").arg(root.wait) : root.detail
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
