#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <atomic>

#include "rdp/KeyMapper.h"

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
    Q_PROPERTY(QSize desktopSize READ desktopSize NOTIFY desktopSizeChanged)
public:
    struct Params
    {
        QString deviceId, title;
        QString host;
        quint16 port = 3389;
        QString username, password, domain;
        QSize size{1600, 1000};
        QString keyMap = QStringLiteral("shortcuts");
        bool lockOnDisconnect = false;   // send Super+L before closing (Ubuntu shared session)
        QString knownFingerprint;        // TOFU: certificate seen on the first connection
    };

    explicit RdpSession(Params params, QObject *parent = nullptr);
    ~RdpSession() override;

    QString deviceId() const { return m_p.deviceId; }
    QString title() const { return m_p.title; }
    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QSize desktopSize() const;

    void start();
    Q_INVOKABLE void close();

    // Thread-safe copy of the current desktop picture.
    QImage frame() const;

    // Input, in remote desktop coordinates. Called from the GUI thread.
    void sendMouseMove(int x, int y);
    void sendMouseButton(Qt::MouseButton button, bool down, int x, int y);
    void sendWheel(int delta, bool horizontal, int x, int y);
    void sendKey(unsigned scancode, bool down);   // raw scancode; Mac shortcut mapping applied here
    void releaseKeys();

    // FreeRDP callbacks (worker thread).
    void onPaint(rdp_gdi *gdi, int x, int y, int w, int h);
    void onResize(rdp_gdi *gdi);
    unsigned onVerifyCertificate(const QString &fingerprint);

signals:
    void stateChanged();
    void desktopSizeChanged();
    void frameUpdated(const QRect &rect);
    void certificateTrusted(const QString &deviceId, const QString &fingerprint);
    void closed(RdpSession *session);

private:
    void run();
    void setState(const QString &state, const QString &error = {});
    void sendScancode(unsigned scancode, bool down);

    Params m_p;
    QThread m_thread;
    std::atomic<rdp_freerdp *> m_instance{nullptr};   // non-null while input may be sent
    std::atomic_bool m_stop{false};
    QString m_state = QStringLiteral("connecting");
    QString m_error;
    mutable QMutex m_frameLock;
    QImage m_frame;
    KeyMapper m_keys;
};
