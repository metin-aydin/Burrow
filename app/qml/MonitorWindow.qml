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
    signal reattach(int monitor)
    signal layoutChanged()

    color: "black"
    title: qsTr("%1 — Display %2").arg(deviceName).arg(monitor + 1)

    onClosing: reattach(monitor)
    onScreenChanged: layoutChanged()
    onVisibilityChanged: layoutChanged()

    RdpView {
        anchors.fill: parent
        session: mw.session
        monitor: mw.monitor
        focus: true
        Component.onCompleted: forceActiveFocus()
    }

    Shortcut { sequence: "Ctrl+Meta+F"; onActivated: mw.visibility === Window.FullScreen ? mw.showNormal() : mw.showFullScreen() }
}
