import QtQuick
import QtQuick.Controls.Basic

// Burrow's context/dropdown menu. Fill it with BMenuItem.
Menu {
    id: menu
    padding: 4
    margins: 8
    background: BPopupSurface { implicitWidth: 180 }
    // A short fade and drop, the same for every menu.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 110; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }
    delegate: BMenuItem {}
}
