pragma Singleton
import QtQuick

// Design tokens. Light or dark as chosen in Settings (App.theme), which sets the app's color scheme.
QtObject {
    readonly property bool dark: Qt.styleHints.colorScheme === Qt.ColorScheme.Dark

    readonly property color bg:            dark ? "#100F0D" : "#F5F3F0"
    readonly property color surface:       dark ? "#1A1815" : "#FFFFFF"
    readonly property color surfaceRaised: dark ? "#23201C" : "#FFFFFF"
    readonly property color surfaceSunken: dark ? "#14120F" : "#EEEBE6"
    readonly property color field:         dark ? "#1F1C18" : "#F8F6F3"
    readonly property color hover:         dark ? "#2A2621" : "#F0EDE8"
    readonly property color border:        dark ? "#2E2A24" : "#E4DFD8"
    readonly property color borderStrong:  dark ? "#40392F" : "#CFC7BC"
    readonly property color text:          dark ? "#ECEEF2" : "#14161B"
    readonly property color textSecondary: dark ? "#9CA3AF" : "#5D6472"
    readonly property color textMuted:     dark ? "#858C99" : "#6B7280"

    // Accent and status colours keep white or soft-background text at 4.5:1 or more.
    readonly property color accent:        dark ? "#9C6B34" : "#96652F"
    readonly property color accentHover:   dark ? "#AD7A40" : "#85592A"
    readonly property color accentText:      "#FFFFFF"
    readonly property color accentSoft:    dark ? "#3A2A18" : "#F5EBDD"
    readonly property color success:       dark ? "#34D399" : "#10A36B"
    readonly property color successSoft:   dark ? "#173528" : "#DDF5EA"
    readonly property color danger:        dark ? "#F87171" : "#C42F2F"
    readonly property color dangerSoft:    dark ? "#3A1D1F" : "#FBE4E4"
    readonly property color warning:       dark ? "#FBBF24" : "#946112"

    // Fixed colours for things drawn over device previews and pictures (same in both themes).
    readonly property color onImage:          "#FFFFFF"
    readonly property color onImageSoft:      "#E6FFFFFF"
    readonly property color onImageMuted:     "#CCFFFFFF"
    readonly property color imageScrim:       "#66000000"
    readonly property color imageScrimHover:  "#99000000"
    readonly property color remoteBackground: "#000000"
    readonly property color modalScrim:    dark ? "#99000000" : "#55141820"
    readonly property color tooltipShadow: dark ? "#80000000" : "#26141820"
    readonly property color popupShadow:   dark ? "#99000000" : "#33141820"
    readonly property color cardShadow:    dark ? "#000000" : "#30202A40"
    // Toasts are inverted against the window.
    readonly property color toastBg:       dark ? "#F2F3F5" : "#1B1E24"
    readonly property color toastText:     dark ? "#14161B" : "#FFFFFF"

    // Tunnel identities. Hues stay clear of the accent (amber), warning, danger (red) and success (green),
    // so a tunnel chip never reads as a status: GlobalProtect denim, Tailscale teal, Cloudflare plum.
    readonly property color gp:            dark ? "#93B7DE" : "#2E5A85"
    readonly property color gpSoft:        dark ? "#1B2633" : "#E2EBF4"
    readonly property color ts:            dark ? "#6FD3C2" : "#0B7466"
    readonly property color tsSoft:        dark ? "#13332F" : "#DDF4F0"
    readonly property color cf:            dark ? "#E0A6D6" : "#7B3C70"
    readonly property color cfSoft:        dark ? "#33202F" : "#F4E5F1"

    readonly property int radius: 10
    readonly property int radiusLarge: 16
    readonly property int controlHeight: 36
    // The main window draws its own header in the title bar on macOS (WindowChrome): its height, and
    // the room the traffic lights take on the left.
    readonly property bool contentTitleBar: Qt.platform.os === "osx"
    readonly property int titleBarHeight: contentTitleBar ? 52 : 0
    readonly property int trafficLightsInset: contentTitleBar ? 96 : 0
    // Controls in the title bar are a little smaller, with the same space above, below and to the right.
    readonly property int titleBarControlHeight: 32
    readonly property int titleBarMargin: (titleBarHeight - titleBarControlHeight) / 2
    readonly property string fontFamily: Qt.application.font.family
    // Only for codes and data people copy or compare character by character (pairing codes), never as decoration.
    readonly property string monoFont: Qt.platform.os === "osx" ? "Menlo" : Qt.platform.os === "windows" ? "Consolas" : "monospace"

    // Type scale by role (system font, macOS density). Use these, never a literal pixelSize.
    // Headings step up in size *and* sit on more space above than below, so a section reads apart
    // from the field labels (fontLabel) and switch titles (fontBody) under it.
    readonly property int fontMicro: 9        // digits inside small indicators
    readonly property int fontCaption: 11     // hints, badges, metadata
    readonly property int fontLabel: 12       // field labels, secondary lines, chips
    readonly property int fontBody: 13        // controls and body text
    readonly property int fontEmphasis: 14    // list row names, inline messages
    readonly property int fontHeading: 15     // section headings, card names
    readonly property int fontTitle: 18       // dialog and window titles
    readonly property int fontDisplay: 22     // lock screen, empty-state title, OTP code

    // Placeholder behind a device card until its first preview: one muted, dark hue per system (top to
    // bottom), dark in both themes so the white on-image text keeps 7:1. The OS badge names the system.
    function osColors(os) {
        return os === "ubuntu" ? ["#4A2E3A", "#33202A"]
             : os === "macos" ? ["#3B3833", "#2A2724"] : ["#34404C", "#262E37"]
    }
}
