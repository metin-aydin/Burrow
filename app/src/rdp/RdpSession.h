#pragma once

#include <QImage>
#include <QRect>
#include <QVariantList>
#include <QMutex>
#include <QWaitCondition>
#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>

#include "rdp/KeyMapper.h"
#include "rdp/RemoteSession.h"
#include "rdp/RdpClipboard.h"
#include <functional>

struct rdp_freerdp;
struct rdp_gdi;

// One RDP connection, driven by FreeRDP 3 on a worker thread (ADR 0001).
// The decoded desktop is kept in `frame()`; input is sent from the GUI thread.
class RdpSession : public RemoteSession
{
    Q_OBJECT
public:
    struct Params
    {
        QString deviceId, title;
        QString host;
        quint16 port = 3389;
        QString username, password, domain;
        QSize size{1600, 1000};
        QString keyMap = QStringLiteral("shortcuts");
        QString clipboard = QStringLiteral("both");
        QString audio = QStringLiteral("local");   // local (play here) | remote (leave on the server) | off
        int scale = 100;                 // desktop scale factor in percent (200 on Retina)
        bool lockOnDisconnect = false;   // send Win/Super+L before closing
        QString knownFingerprint;        // pin "host:port|sha256" confirmed by the user earlier
        QString pinHost;                 // the device address the pin belongs to (host may be a tunnel forward)
        quint16 pinPort = 0;
        int tunnelHandle = 0;            // netcore tunnel to carry the connection (0 = direct)
        struct Monitor { QRect rect; int screen = 0; };
        QList<Monitor> monitors;         // 2+ = span these displays (first is primary); size = bounding box
    };

    explicit RdpSession(Params params, QObject *parent = nullptr);
    ~RdpSession() override;

    QString deviceId() const override { return m_p.deviceId; }
    QString title() const override { return m_p.title; }
    QString state() const override { return m_state; }
    QString error() const override { return m_error; }
    QString phase() const override { return m_phase; }
    void setPhase(const QString &phase);   // any thread
    QSize desktopSize() const override;
    QVariantList monitors() const override;
    bool multiMonitor() const override;
    bool wasConnected() const override { return m_wasConnected; }

    void start() override;
    void close() override;
    // Answer to certificatePrompt (GUI thread).
    void answerCertificate(bool trust) override;

    // Thread-safe copy of the current desktop picture.
    QImage frame() const override;
    QImage frame(const QRect &part) const override;
    void copyInto(QImage &dst, const QRect &part, const QPoint &at) const override;

    // Input, in remote desktop coordinates. Called from the GUI thread.
    void sendMouseMove(int x, int y) override;
    void sendMouseButton(Qt::MouseButton button, bool down, int x, int y) override;
    void sendWheel(int delta, bool horizontal, int x, int y) override;
    void sendKey(unsigned scancode, bool down) override;   // raw scancode; Mac shortcut mapping applied here
    void releaseKeys() override;
    // Asks the server to resize the desktop (pixels, scale %). Debounce in the caller.
    void requestResize(const QSize &pixels, int scale) override;
    // With several monitors: asks for monitor `index` to have this size (the window showing it). The
    // whole layout is sent shortly after the last change, monitors side by side in index order.
    void requestMonitorSize(int index, const QSize &pixels) override;

    // FreeRDP callbacks (worker thread).
    void onPaint(rdp_gdi *gdi, int x, int y, int w, int h);
    void onResize(rdp_gdi *gdi);
    unsigned onVerifyCertificate(const QString &fingerprint);
    void onChannelConnected(const char *name, void *iface);
    void onChannelDisconnected(const char *name);
    void onServerMonitors(const QList<QRect> &monitors, const QSize &desktop, bool handedOver);   // worker thread
    void *origResetGraphics = nullptr;   // the GDI's GFX reset handler we chain to
    int connectSocket(const char *hostname, int port);   // TCPConnect replacement (worker thread)
    bool clipboardEnabled() const { return m_clip && m_clip->enabled(); }
    int scale() const override { return m_p.scale; }

private:
    void run();
    void setState(const QString &state, const QString &error = {});
    void sendScancode(unsigned scancode, bool down);
    void post(std::function<void()> task);   // run on the worker thread
    void drainTasks();
    QString pinFor(const QString &fingerprint) const;

    Params m_p;
    QThread m_thread;
    std::atomic<rdp_freerdp *> m_instance{nullptr};   // non-null while input may be sent
    std::atomic_bool m_stop{false};
    QString m_state = QStringLiteral("connecting");
    QString m_error;
    QString m_phase = QStringLiteral("connecting");
    bool m_wasConnected = false;
    mutable QMutex m_frameLock;
    QImage m_frame;
    KeyMapper m_keys;
    QMutex m_certLock;
    QWaitCondition m_certCond;
    int m_certAnswer = -1;
    std::atomic_bool m_certChanged{false};
    RdpClipboard *m_clip = nullptr;
    void *m_disp = nullptr;                  // DispClientContext*, worker thread only
    QList<QRect> m_serverMonitors;           // guarded by m_frameLock
    QList<QSize> m_wantedSizes;              // GUI thread: monitor sizes to ask for
    QTimer *m_layoutTimer = nullptr;
    void sendMonitorLayout();
    // A layout change makes the server rebuild its screens; another one before that finishes (or right
    // after connecting, while GNOME hands the session over) makes it end the session. So: one at a time.
    bool m_layoutPending = false;
    int m_layoutRetries = 0;      // re-asks after the server went back to other sizes
    // The server handed the session over (GNOME Remote Login). Such servers end the session or hand it
    // over again when the monitor layout changes while connected, so it is never changed live: the
    // sizes the windows want are saved and sent when the next connection starts.
    bool m_handedOver = false;
    bool m_serverLayoutSeen = false;   // the server told its layout (it may still hand over before)
    QElapsedTimer m_layoutSent;   // since the pending request went out
    QElapsedTimer m_settled;      // since the desktop last (re)appeared or changed
    QMutex m_tasksLock;
    QList<std::function<void()>> m_tasks;
};
