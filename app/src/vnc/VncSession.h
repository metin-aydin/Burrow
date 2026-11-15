#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <atomic>

#include "rdp/RemoteSession.h"

struct z_stream_s;

// One VNC (RFB 3.8) connection to a Mac's Screen Sharing / Remote Management or any VNC server
// (ADR 0008). Our own client: Apple's sign-in (security type 30, Mac user name and password),
// classic VNC password (type 2) or none; Raw, CopyRect and ZRLE pictures; keys are sent as X
// keysyms. The socket is read on a worker thread; input is written from the GUI thread.
class VncSession : public RemoteSession
{
    Q_OBJECT
public:
    struct Params
    {
        QString deviceId, title;
        QString host;            // the device's address, reached through the tunnel (or directly)
        quint16 port = 5900;
        QString username, password;
        QString clipboard = QStringLiteral("both");   // both | to_remote | to_local | off
        int tunnelHandle = 0;    // netcore tunnel (0 = direct)
    };

    explicit VncSession(Params params, QObject *parent = nullptr);
    ~VncSession() override;

    QString deviceId() const override { return m_p.deviceId; }
    QString title() const override { return m_p.title; }
    QString state() const override { return m_state; }
    QString error() const override { return m_error; }
    QString phase() const override { return m_phase; }
    QSize desktopSize() const override;
    bool wasConnected() const override { return m_wasConnected; }

    void start() override;
    void close() override;

    QImage frame() const override;
    QImage frame(const QRect &part) const override;

    void sendMouseMove(int x, int y) override;
    void sendMouseButton(Qt::MouseButton button, bool down, int x, int y) override;
    void sendWheel(int delta, bool horizontal, int x, int y) override;
    void sendKey(unsigned scancode, bool down) override;
    bool usesKeysyms() const override { return true; }
    void sendKeysym(quint32 keysym, bool down) override;
    void releaseKeys() override;

    // Exposed for tests: the VNC password response and the Apple sign-in block.
    static QByteArray vncAuthResponse(const QByteArray &challenge, const QString &password);
    static quint32 keysymForScancode(unsigned scancode);

private:
    void run();
    QString handshake();          // "" when signed in and initialised, else the error to show
    QString authApple();
    QString authVnc();
    bool messageLoop();
    bool readFramebufferUpdate();
    bool readRaw(int x, int y, int w, int h);
    bool readCopyRect(int x, int y, int w, int h);
    bool readZrle(int x, int y, int w, int h);
    void resizeFrame(int w, int h);
    void requestUpdate(bool incremental);
    bool readExact(void *buf, size_t n);
    bool writeAll(const void *buf, size_t n);   // any thread
    void setState(const QString &state, const QString &error = {});
    void setPhase(const QString &phase);
    void sendPointer();
    void onLocalClipboard();      // GUI thread

    Params m_p;
    QThread m_thread;
    std::atomic_bool m_stop{false};
    std::atomic_int m_fd{-1};
    QMutex m_writeLock;
    QString m_state = QStringLiteral("connecting");
    QString m_error;
    QString m_phase = QStringLiteral("connecting");
    bool m_wasConnected = false;
    mutable QMutex m_frameLock;
    QImage m_frame;
    z_stream_s *m_zrle = nullptr;   // one zlib stream for the whole connection (worker thread)
    QByteArray m_zbuf;
    // Pointer state (GUI thread).
    quint8 m_buttons = 0;
    int m_px = 0, m_py = 0;
    QSet<quint32> m_heldKeys;       // keysyms down on the remote side (GUI thread)
    QString m_lastClipboard;        // last text exchanged, to avoid echoing it back
};
