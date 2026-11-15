import QtQuick
import QtQuick.Controls.Basic

// Burrow's tooltip: a small raised label under its control, shown after a short pause, fading in.
// Use through BButton.tip, or directly: BToolTip { visible: someArea.containsMouse; text: "…" }.
ToolTip {
    id: tip
    delay: 550
    timeout: -1
    // Under the control, centred on it, a few points away.
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? parent.height + 6 : 0
    margins: 8
    topPadding: 5
    bottomPadding: 6
    leftPadding: 9
    rightPadding: 9
    font.pixelSize: 12
    font.weight: Font.Medium

    contentItem: Text {
        text: tip.text
        font: tip.font
        color: Theme.text
        wrapMode: Text.WordWrap
    }
    background: BPopupSurface { radius: 7 }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }
}
