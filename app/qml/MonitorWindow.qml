import QtQuick
import QtQuick.Window
import Burrow.Core

// One remote monitor moved out of the session window (dragged to another display). Closing it puts
// the monitor back as a tab; the connection stays open.
Window {
    id: mw
    property RdpSession session: null
    property int monitor: 0
    property string deviceName
    property string displayName
    signal reattach(int monitor)
    signal layoutChanged()

    color: "black"
    title: deviceName + " — " + displayName
    // Full screen once the window is on its display (see SessionWindow.detach).
    function goFullScreen(delay) { fullTimer.interval = delay; fullTimer.start() }
    Timer { id: fullTimer; onTriggered: mw.showFullScreen() }

    onClosing: reattach(monitor)
    onScreenChanged: layoutChanged()
    onVisibilityChanged: layoutChanged()
    // The size this monitor's picture has here; saved so the next connection starts with it.
    readonly property size viewSize: Qt.size(mv.width, mv.height)
    onViewSizeChanged: layoutChanged()

    RdpView {
        id: mv
        anchors.fill: parent
        session: mw.session
        monitor: mw.monitor
        focus: true
        Component.onCompleted: forceActiveFocus()
    }

    Shortcut { sequence: "Ctrl+Meta+F"; onActivated: mw.visibility === Window.FullScreen ? mw.showNormal() : mw.showFullScreen() }
}
