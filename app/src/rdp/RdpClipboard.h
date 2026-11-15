#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <functional>

struct s_cliprdr_client_context;

// Two-way clipboard over the RDP cliprdr channel (SPEC §5.0): plain text and images both ways,
// and files copied on the remote computer (remote → this Mac only). Local clipboard access happens
// on the GUI thread; channel callbacks arrive on FreeRDP's thread and only touch the cached copy
// under a mutex. Clipboard contents are never logged. Files are the one exception to "never on
// disk": they are downloaded into a private per-session folder (see filesRoot()) that is removed
// when the session ends.
class RdpClipboard : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Both, ToRemote, ToLocal, Off };
    static Mode modeFromString(const QString &s);

    // Largest set of files taken from the remote clipboard at once.
    static constexpr quint64 kMaxFileBytes = 2ull * 1024 * 1024 * 1024;
    static constexpr int kMaxFiles = 10000;

    // `post` runs a function on the FreeRDP worker thread (where channel calls are made).
    RdpClipboard(Mode mode, std::function<void(std::function<void()>)> post, QObject *parent = nullptr);
    ~RdpClipboard() override;

    bool enabled() const { return m_mode != Mode::Off; }

    // Channel lifecycle (worker thread).
    void attach(s_cliprdr_client_context *ctx);
    void detach();

    // One entry of a remote file list (FileGroupDescriptorW).
    struct RemoteFile {
        QString path;        // relative, '/' separated, checked to stay inside the target folder
        bool dir = false;
        bool hasSize = false;
        quint64 size = 0;
        QDateTime modified;  // invalid when the remote side didn't send it
    };

    // Converters (public for tests).
    static QByteArray imageToDib(const QImage &image);
    static QImage dibToImage(const QByteArray &dib);
    // Parses a CLIPRDR_FILELIST; false when it is malformed or names an unsafe path.
    static bool parseFileList(const QByteArray &data, QList<RemoteFile> *out);
    // "dir\\name.txt" → "dir/name.txt"; empty when the name is absolute or leaves the folder.
    static QString safeRelativePath(const QString &remoteName);

    // Callbacks from the channel (worker thread).
    struct ServerFormat { unsigned id; QString name; };
    unsigned onMonitorReady();
    void onServerCapabilities(unsigned generalFlags);
    unsigned onServerFormatList(const QList<ServerFormat> &formats);
    unsigned onServerFormatDataResponse(bool ok, const QByteArray &data);
    unsigned onServerFormatDataRequest(unsigned format);
    unsigned onServerFileContentsResponse(bool ok, unsigned streamId, const QByteArray &data);

signals:
    void notice(const QString &message);   // a short line for the session window (any thread)

private:
    void onLocalChanged();           // GUI thread
    unsigned sendFormatList();       // worker thread
    bool toRemote() const { return m_mode == Mode::Both || m_mode == Mode::ToRemote; }
    bool toLocal() const { return m_mode == Mode::Both || m_mode == Mode::ToLocal; }

    // File download (worker thread).
    QString filesRoot() const;
    void startFiles(const QList<RemoteFile> &files);
    unsigned nextFileStep();
    unsigned requestContents(unsigned flags, quint64 position, quint32 bytes);
    void finishFiles();
    void failFiles(const QString &message);
    void abortFiles();

    Mode m_mode;
    std::function<void(std::function<void()>)> m_post;
    s_cliprdr_client_context *m_ctx = nullptr;   // worker thread only
    unsigned m_requested = 0;                    // worker thread only
    bool m_ready = false;                        // worker thread only
    bool m_serverFiles = false;                  // server can stream files (worker thread)
    unsigned m_fileFormat = 0;                   // id of FileGroupDescriptorW in the last list

    // Current download (worker thread).
    QString m_sessionDir;                        // created lazily, removed in the destructor
    QString m_batchDir;
    int m_batch = 0;
    QList<RemoteFile> m_files;
    int m_fileIndex = -1;
    quint64 m_offset = 0;
    quint64 m_total = 0;
    unsigned m_streamId = 0;
    bool m_sizeRequest = false;
    QFile m_out;

    QMutex m_lock;                               // guards the cached local content
    QString m_text;
    QByteArray m_dib;
    bool m_ignoreNextChange = false;             // GUI thread: our own setText/setImage/setUrls
};
