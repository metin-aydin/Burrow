#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>

#include "rdp/RemoteSession.h"

struct z_stream_s;
struct evp_cipher_ctx_st;

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
        bool lockOnDisconnect = false;   // Control+Command+Q before closing (a Mac)
    };

    explicit VncSession(Params params, QObject *parent = nullptr);
    ~VncSession() override;

    QString deviceId() const override { return m_p.deviceId; }
    QString title() const override { return m_p.title; }
    QString state() const override { return m_state; }
    QString error() const override { return m_error; }
    QString phase() const override { return m_phase; }
    QSize desktopSize() const override;
    QVariantList monitors() const override;
    bool multiMonitor() const override;
    bool wasConnected() const override { return m_wasConnected; }

    void start() override;
    void close() override;

    QImage frame() const override;
    QImage frame(const QRect &part) const override;
    void copyInto(QImage &dst, const QRect &part, const QPoint &at) const override;

    void sendMouseMove(int x, int y) override;
    void sendMouseButton(Qt::MouseButton button, bool down, int x, int y) override;
    void sendWheel(int delta, bool horizontal, int x, int y) override;
    void sendKey(unsigned scancode, bool down) override;
    bool usesKeysyms() const override { return true; }
    void sendKeysym(quint32 keysym, bool down) override;
    void releaseKeys() override;
    QImage cursorImage() const override { return m_cursor; }
    QPoint cursorHotspot() const override { return m_cursorHot; }

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
    bool readExtendedDesktopSize(int w, int h);
    void resizeFrame(int w, int h);
    void requestUpdate(bool incremental);
    bool readExact(void *buf, size_t n);        // through the record layer once it is up
    bool writeAll(const void *buf, size_t n);   // any thread; one whole message per call
    bool rawRead(void *buf, size_t n);
    bool rawWrite(const void *buf, size_t n);   // caller holds m_writeLock
    // Apple's revision (RFB 003.889, ADR 0008): after the rekey every message travels in records
    // (AES-128-CBC per direction, SHA-1 integrity), and the Mac reports its displays.
    QString appleSetup();
    bool fillRecord();
    bool startRecords(const uchar *rekeyBody);
    bool readAppleLayout();
    bool readAppleCursor(int hx, int hy, int w, int h);
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
    QList<QRect> m_screens;          // ExtendedDesktopSize screens (guarded by m_frameLock)
    bool m_screensReported = false;  // worker thread: the server sent ExtendedDesktopSize
    bool m_firstUpdate = true;       // worker thread
    std::atomic_bool m_apple{false};
    uchar m_wrapKey[16] = {};
    bool m_wrapKeyValid = false;     // set only by a completed Apple sign-in
    std::atomic_bool m_integrityFailed{false};
    bool m_writeBroken = false;      // guarded by m_writeLock
    void failRecords();              // a record couldn't be sent: the chain is broken, end the session
    evp_cipher_ctx_st *m_inCtx = nullptr, *m_outCtx = nullptr;
    quint32 m_inSeq = 0, m_outSeq = 0;
    std::atomic_bool m_records{false};
    QByteArray m_inBuf;              // decrypted bytes not read yet (worker thread)
    qsizetype m_inPos = 0;
    bool m_armed = false;            // worker thread: Apple pushes updates (AutoFrameBufferUpdate)
    z_stream_s *m_zrle = nullptr;   // one zlib stream for the whole connection (worker thread)
    QByteArray m_zbuf;
    // Pointer state (GUI thread).
    quint8 m_buttons = 0;
    int m_px = 0, m_py = 0;
    QSet<quint32> m_heldKeys;       // keysyms down on the remote side (GUI thread)
    QString m_lastClipboard;
    struct CursorShape { QImage image; QPoint hotspot; };
    QHash<quint32, CursorShape> m_cursorCache;   // worker thread: Apple's stored pointer shapes
    QImage m_cursor;                // GUI thread: current remote pointer shape
    QPoint m_cursorHot;
    // Throughput, logged every 5 s while pictures arrive (worker thread, except the copy counters).
    QElapsedTimer m_statClock;
    quint64 m_statBytes = 0, m_statPixels = 0;
    int m_statUpdates = 0;
    qint64 m_statDecodeNs = 0;
    qint64 m_statCpuNs = 0;          // inflate + decode only, without waiting for bytes
    mutable std::atomic<qint64> m_statCopyNs{0};
    mutable std::atomic<int> m_statCopies{0};
    void logStats();        // last text exchanged, to avoid echoing it back
};
