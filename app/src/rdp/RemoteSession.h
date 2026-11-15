#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QVariantList>

// A remote desktop connection as the UI sees it, whatever the protocol: RDP (RdpSession) or VNC
// (VncSession, ADR 0008). The view (RdpView) and the session windows only use this interface.
class RemoteSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceId READ deviceId CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)       // connecting | connected | closed
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)   // connecting | certificate | signing_in | opening | connected
    Q_PROPERTY(QSize desktopSize READ desktopSize NOTIFY desktopSizeChanged)
    // [{x, y, width, height}] in desktop pixels when the desktop has several monitors; [] = one picture.
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY desktopSizeChanged)
    Q_PROPERTY(bool multiMonitor READ multiMonitor NOTIFY desktopSizeChanged)
    Q_PROPERTY(bool wasConnected READ wasConnected NOTIFY stateChanged)
    Q_PROPERTY(int scale READ scale CONSTANT)   // percent: 100 logical pixels, 200 Retina
public:
    using QObject::QObject;

    virtual QString deviceId() const = 0;
    virtual QString title() const = 0;
    virtual QString state() const = 0;
    virtual QString error() const = 0;
    virtual QString phase() const = 0;
    virtual QSize desktopSize() const = 0;
    virtual QVariantList monitors() const { return {}; }
    virtual bool multiMonitor() const { return false; }
    virtual bool wasConnected() const = 0;
    virtual int scale() const { return 100; }

    virtual void start() = 0;
    Q_INVOKABLE virtual void close() = 0;
    // Answer to certificatePrompt (GUI thread). Protocols without certificates ignore it.
    Q_INVOKABLE virtual void answerCertificate(bool trust) { Q_UNUSED(trust) }

    // Thread-safe copies of the current desktop picture (whole, or one part such as a monitor).
    virtual QImage frame() const = 0;
    virtual QImage frame(const QRect &part) const = 0;
    // Copies `part` of the desktop into `dst` at `at` (same RGB32 format), without a temporary image:
    // a view keeps its own picture and refreshes only what changed.
    virtual void copyInto(QImage &dst, const QRect &part, const QPoint &at) const = 0;

    // Input, in remote desktop coordinates, from the GUI thread. Keys are RDP scancodes (the view's
    // common currency); other protocols translate them.
    virtual void sendMouseMove(int x, int y) = 0;
    virtual void sendMouseButton(Qt::MouseButton button, bool down, int x, int y) = 0;
    virtual void sendWheel(int delta, bool horizontal, int x, int y) = 0;
    virtual void sendKey(unsigned scancode, bool down) = 0;
    virtual void releaseKeys() = 0;
    // VNC takes X keysyms (the character typed) rather than scancodes; the view sends those instead.
    virtual bool usesKeysyms() const { return false; }
    virtual void sendKeysym(quint32 keysym, bool down) { Q_UNUSED(keysym) Q_UNUSED(down) }
    // The view's size changed (pixels, scale %); protocols that can't resize the remote desktop ignore it.
    virtual void requestResize(const QSize &pixels, int scale) { Q_UNUSED(pixels) Q_UNUSED(scale) }
    virtual void requestMonitorSize(int index, const QSize &pixels) { Q_UNUSED(index) Q_UNUSED(pixels) }
    // The remote pointer's shape, when the protocol sends one (null image = keep the local arrow).
    virtual QImage cursorImage() const { return {}; }
    virtual QPoint cursorHotspot() const { return {}; }

signals:
    void stateChanged();
    void phaseChanged();
    void desktopSizeChanged();
    void frameUpdated(const QRect &rect);
    void certificatePrompt(const QString &address, const QString &fingerprint);
    void certificateTrusted(const QString &deviceId, const QString &pin);
    void closed(RemoteSession *session);
    void cursorChanged();   // GUI thread
};
