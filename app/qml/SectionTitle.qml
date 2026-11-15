import QtQuick
import QtQuick.Layouts

// Heading for a group of fields in a dialog. One step above field labels (fontLabel) and switch titles
// (fontBody), with clearly more space above than below so each group reads as its own block.
Text {
    property bool first: false   // the dialog's first section sits right under its title: no extra gap
    Layout.fillWidth: true
    Layout.topMargin: first ? 0 : 14
    color: Theme.text
    font.pixelSize: Theme.fontHeading
    font.weight: Font.DemiBold
    Accessible.role: Accessible.Heading
    Accessible.name: text
}
