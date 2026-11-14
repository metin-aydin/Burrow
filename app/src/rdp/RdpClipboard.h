#pragma once

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <functional>

struct s_cliprdr_client_context;

// Two-way clipboard over the RDP cliprdr channel (SPEC §5.0): plain text and images.
// Local clipboard access happens on the GUI thread; channel callbacks arrive on FreeRDP's
// thread and only touch the cached copy under a mutex. Clipboard contents are never logged
// or written to disk.
class RdpClipboard : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Both, ToRemote, ToLocal, Off };
    static Mode modeFromString(const QString &s);

    // `post` runs a function on the FreeRDP worker thread (where channel calls are made).
    RdpClipboard(Mode mode, std::function<void(std::function<void()>)> post, QObject *parent = nullptr);
    ~RdpClipboard() override;

    bool enabled() const { return m_mode != Mode::Off; }

    // Channel lifecycle (worker thread).
    void attach(s_cliprdr_client_context *ctx);
    void detach();

    // Converters (public for tests).
    static QByteArray imageToDib(const QImage &image);
    static QImage dibToImage(const QByteArray &dib);

    // Callbacks from the channel (worker thread).
    unsigned onMonitorReady();
    unsigned onServerFormatList(const QList<unsigned> &formats);
    unsigned onServerFormatDataResponse(bool ok, const QByteArray &data);
    unsigned onServerFormatDataRequest(unsigned format);

private:
    void onLocalChanged();           // GUI thread
    unsigned sendFormatList();       // worker thread
    bool toRemote() const { return m_mode == Mode::Both || m_mode == Mode::ToRemote; }
    bool toLocal() const { return m_mode == Mode::Both || m_mode == Mode::ToLocal; }

    Mode m_mode;
    std::function<void(std::function<void()>)> m_post;
    s_cliprdr_client_context *m_ctx = nullptr;   // worker thread only
    unsigned m_requested = 0;                    // worker thread only
    bool m_ready = false;                        // worker thread only

    QMutex m_lock;                               // guards the cached local content
    QString m_text;
    QByteArray m_dib;
    bool m_ignoreNextChange = false;             // GUI thread: our own setText/setImage
};
