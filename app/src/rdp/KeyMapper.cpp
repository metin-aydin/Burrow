#include "rdp/KeyMapper.h"

#include <QString>

KeyMapper::Mode KeyMapper::modeFromString(const QString &s)
{
    if (s == QLatin1String("swap"))
        return Mode::Swap;
    if (s == QLatin1String("off"))
        return Mode::Off;
    return Mode::Shortcuts;
}

bool KeyMapper::isShortcutKey(unsigned sc)
{
    // A C V X Z Y S F P W T N O R L  (set-1 scancodes)
    switch (sc) {
    case 0x1E: case 0x2E: case 0x2F: case 0x2D: case 0x2C: case 0x15: case 0x1F: case 0x21:
    case 0x19: case 0x11: case 0x14: case 0x31: case 0x18: case 0x13: case 0x26:
        return true;
    default:
        return false;
    }
}

QList<KeyMapper::Event> KeyMapper::map(unsigned sc, bool down)
{
    if (m_mode == Mode::Off)
        return {{sc, down}};

    if (m_mode == Mode::Swap) {
        if (isCmd(sc))
            return {{LCtrl, down}};
        if (isCtrl(sc))
            return {{LWin, down}};
        return {{sc, down}};
    }

    // Shortcuts mode: hold back Cmd until we know what it is combined with.
    if (isCmd(sc)) {
        if (down) {
            m_cmdHeld = true;
            return {};
        }
        QList<Event> out;
        if (m_ctrlSubstituted)
            out.append({LCtrl, false});
        else if (m_cmdSent)
            out.append({LWin, false});
        else
            out = {{LWin, true}, {LWin, false}}; // Cmd tapped alone → Start / Activities
        reset();
        return out;
    }

    if (m_cmdHeld && down && !m_cmdSent && !m_ctrlSubstituted) {
        if (isShortcutKey(sc)) {
            m_ctrlSubstituted = true;
            return {{LCtrl, true}, {sc, true}};
        }
        m_cmdSent = true;
        return {{LWin, true}, {sc, true}};
    }
    return {{sc, down}};
}

QList<KeyMapper::Event> KeyMapper::releaseAll()
{
    QList<Event> out;
    if (m_ctrlSubstituted)
        out.append({LCtrl, false});
    if (m_cmdSent)
        out.append({LWin, false});
    reset();
    return out;
}
