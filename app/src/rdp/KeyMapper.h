#pragma once

#include <QList>

// Translates local key presses into RDP scancode events, applying the device's
// Mac shortcut mode (SPEC §5.0.1). Pure logic so it can be unit-tested.
//
// Modes:
//  - shortcuts: Cmd+<common key> is sent as Ctrl+<key>; Cmd alone or with other
//    keys still acts as the Windows/Super key.
//  - swap: Cmd and Ctrl are swapped everywhere.
//  - off: keys are sent as they are (Cmd = Windows/Super).
class KeyMapper
{
public:
    enum class Mode { Shortcuts, Swap, Off };

    struct Event
    {
        unsigned scancode; // RDP scancode incl. extended flag (FreeRDP RDP_SCANCODE_*)
        bool down;
        bool operator==(const Event &o) const { return scancode == o.scancode && down == o.down; }
    };

    // Well-known scancodes (FreeRDP RDP_SCANCODE_* values).
    static constexpr unsigned LCtrl = 0x1D;
    static constexpr unsigned LWin = 0x15B;   // extended
    static constexpr unsigned RWin = 0x15C;   // extended

    explicit KeyMapper(Mode mode = Mode::Shortcuts) : m_mode(mode) {}
    void setMode(Mode m) { m_mode = m; reset(); }
    static Mode modeFromString(const QString &s);

    // `scancode` is the key's RDP scancode as the keyboard reports it.
    QList<Event> map(unsigned scancode, bool down);

    // Releases anything still held (window lost focus, disconnect).
    QList<Event> releaseAll();
    void reset() { m_cmdHeld = false; m_cmdSent = false; m_ctrlSubstituted = false; }

private:
    static bool isCmd(unsigned sc) { return sc == LWin || sc == RWin; }
    static bool isCtrl(unsigned sc) { return sc == LCtrl || sc == 0x11D; }
    static bool isShortcutKey(unsigned sc);

    Mode m_mode;
    bool m_cmdHeld = false;          // physical Cmd is down
    bool m_cmdSent = false;          // a Win key-down was sent for it
    bool m_ctrlSubstituted = false;  // a Ctrl key-down was sent in its place
};
