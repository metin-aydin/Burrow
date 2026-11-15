#pragma once

#include <QImage>
#include <QRect>
#include <QVariantList>
#include <QMutex>
#include <QWaitCondition>
#include <QObject>
#include <QThread>
#include <atomic>

#include "rdp/KeyMapper.h"
#include "rdp/RdpClipboard.h"
#include <functional>

struct rdp_freerdp;
struct rdp_gdi;

// One RDP connection, driven by FreeRDP 3 on a worker thread (ADR 0001).
// The decoded desktop is kept in `frame()`; input is sent from the GUI thread.
class RdpSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceId READ deviceId CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)       // connecting | connected | closed
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)   // connecting | certificate | signing_in | opening | connected
    Q_PROPERTY(QSize desktopSize READ desktopSize NOTIFY desktopSizeChanged)
    // [{x, y, width, height, screen}] in desktop pixels when spanning several Mac displays.
    Q_PROPERTY(QVariantList monitors READ monitors CONSTANT)
    // True once the server accepted the multi-monitor layout (some, like GNOME screen sharing, don't).
    Q_PROPERTY(bool multiMonitor READ multiMonitor NOTIFY desktopSizeChanged)
    Q_PROPERTY(bool wasConnected READ wasConnected NOTIFY stateChanged)
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

    QString deviceId() const { return m_p.deviceId; }
    QString title() const { return m_p.title; }
    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QString phase() const { return m_phase; }
    void setPhase(const QString &phase);   // any thread
    QSize desktopSize() const;
    QVariantList monitors() const;
    bool multiMonitor() const;
    bool wasConnected() const { return m_wasConnected; }

    void start();
    Q_INVOKABLE void close();
    // Answer to certificatePrompt (GUI thread).
    Q_INVOKABLE void answerCertificate(bool trust);

    // Thread-safe copy of the current desktop picture.
    QImage frame() const;

    // Input, in remote desktop coordinates. Called from the GUI thread.
    void sendMouseMove(int x, int y);
    void sendMouseButton(Qt::MouseButton button, bool down, int x, int y);
    void sendWheel(int delta, bool horizontal, int x, int y);
    void sendKey(unsigned scancode, bool down);   // raw scancode; Mac shortcut mapping applied here
    void releaseKeys();
    // Asks the server to resize the desktop (pixels, scale %). Debounce in the caller.
    void requestResize(const QSize &pixels, int scale);

    // FreeRDP callbacks (worker thread).
    void onPaint(rdp_gdi *gdi, int x, int y, int w, int h);
    void onResize(rdp_gdi *gdi);
    unsigned onVerifyCertificate(const QString &fingerprint);
    void onChannelConnected(const char *name, void *iface);
    void onChannelDisconnected(const char *name);
    int connectSocket(const char *hostname, int port);   // TCPConnect replacement (worker thread)
    bool clipboardEnabled() const { return m_clip && m_clip->enabled(); }
    int scale() const { return m_p.scale; }

signals:
    void stateChanged();
    void phaseChanged();
    void desktopSizeChanged();
    void frameUpdated(const QRect &rect);
    void certificatePrompt(const QString &address, const QString &fingerprint);
    void certificateTrusted(const QString &deviceId, const QString &pin);
    void closed(RdpSession *session);

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
    QMutex m_tasksLock;
    QList<std::function<void()>> m_tasks;
};
