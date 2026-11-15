import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes

// Animated step list for connection progress. `steps` is [{id, label}]; `current` is the active step
// id ("" = not started, "done" = all finished); `failed` marks the active step as failed; `wait` shows
// a countdown on the active step (seconds) and `waitTotal` its full length.
// Every row has the same height and every marker the same size, so nothing moves as steps finish;
// the active step's detail goes in a reserved line under the list.
ColumnLayout {
    id: root
    property var steps: []
    property string current: ""
    property bool failed: false
    property int wait: 0
    property int waitTotal: 30
    property string detail: ""
    readonly property int marker: 22
    spacing: 6

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
            Layout.preferredHeight: 26
            spacing: 12
            opacity: done || active ? 1 : 0.5
            Behavior on opacity { NumberAnimation { duration: 250 } }

            Item {
                Layout.preferredWidth: root.marker
                Layout.preferredHeight: root.marker
                // Pending: hollow circle of the same size as every other marker.
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    visible: !row.done && !row.active
                    color: "transparent"
                    border.color: Theme.borderStrong
                    border.width: 2
                }
                // Done: a filled circle with a check that fades in (no size change).
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.success
                    opacity: row.done ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 220 } }
                    Icon { anchors.centerIn: parent; name: "check"; size: 13; color: Theme.onImage }
                }
                // Active: spinner, or a countdown ring while waiting.
                Spinner {
                    anchors.fill: parent
                    size: root.marker
                    visible: row.active && !root.failed && root.wait <= 0
                    running: visible
                }
                Shape {
                    anchors.fill: parent
                    visible: row.active && !root.failed && root.wait > 0
                    layer.enabled: true
                    layer.samples: 4
                    ShapePath {
                        strokeColor: Qt.alpha(Theme.warning, 0.25); strokeWidth: 2.5; fillColor: "transparent"
                        PathAngleArc { centerX: 11; centerY: 11; radiusX: 9.5; radiusY: 9.5; startAngle: 0; sweepAngle: 360 }
                    }
                    ShapePath {
                        strokeColor: Theme.warning; strokeWidth: 2.5; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                        PathAngleArc {
                            centerX: 11; centerY: 11; radiusX: 9.5; radiusY: 9.5; startAngle: -90
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
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                // Failed: red circle with a cross.
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    visible: row.active && root.failed
                    color: Theme.danger
                    Icon { anchors.centerIn: parent; name: "x"; size: 12; color: Theme.onImage }
                }
            }
            Text {
                Layout.fillWidth: true
                text: row.modelData.label
                color: row.active && root.failed ? Theme.danger : Theme.text
                font.pixelSize: 13
                font.weight: row.active ? Font.Medium : Font.Normal
                elide: Text.ElideRight
            }
        }
    }
    // Detail of the active step (OTP countdown, browser sign-in, certificate…). The line is always
    // reserved so the list doesn't shift when a detail appears.
    Text {
        Layout.fillWidth: true
        Layout.leftMargin: root.marker + 12
        Layout.minimumHeight: 16
        text: root.failed ? "" : root.wait > 0 ? qsTr("Waiting for the next one-time code… %1 s").arg(root.wait) : root.detail
        color: Theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
}
