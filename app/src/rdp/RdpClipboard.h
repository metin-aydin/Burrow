#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

struct s_cliprdr_client_context;

// Two-way clipboard over the RDP cliprdr channel (SPEC §5.0): plain text, images and files, both
// ways, on every platform Qt runs on. Local clipboard access happens
// on the GUI thread; channel callbacks arrive on FreeRDP's thread and only touch the cached copy
// under a mutex. Clipboard contents are never logged. Files from the remote side are the one
// exception to "never on disk": they are downloaded into a private per-session folder (see
// filesRoot()) that is removed when the session ends. Local files are read only when the remote
// side asks for a piece of one, and only files that were copied on this computer.
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

    // One entry of a local file list offered to the remote side.
    struct LocalFile {
        QString absolutePath;
        QString remoteName;  // relative, '\\' separated, as FileGroupDescriptorW wants it
        bool dir = false;
        quint64 size = 0;
        QDateTime modified;
    };

    // Converters (public for tests).
    static QByteArray imageToDib(const QImage &image);
    static QImage dibToImage(const QByteArray &dib);
    // Parses a CLIPRDR_FILELIST; false when it is malformed or names an unsafe path.
    static bool parseFileList(const QByteArray &data, QList<RemoteFile> *out);
    // "dir\\name.txt" → "dir/name.txt"; empty when the name is absolute or leaves the folder.
    static QString safeRelativePath(const QString &remoteName);
    // Expands copied files and folders (folders first, then their contents; symlinks inside folders
    // are skipped). Returns false when there are more than kMaxFiles entries.
    static bool collectLocalFiles(const QList<QUrl> &urls, QList<LocalFile> *out);
    // Serializes a CLIPRDR_FILELIST for the remote side.
    static QByteArray buildFileList(const QList<LocalFile> &files);

    // Callbacks from the channel (worker thread).
    struct ServerFormat { unsigned id; QString name; };
    unsigned onMonitorReady();
    void onServerCapabilities(unsigned generalFlags);
    unsigned onServerFormatList(const QList<ServerFormat> &formats);
    unsigned onServerFormatDataResponse(bool ok, const QByteArray &data);
    unsigned onServerFormatDataRequest(unsigned format);
    unsigned onServerFileContentsResponse(bool ok, unsigned streamId, const QByteArray &data);
    unsigned onServerFileContentsRequest(unsigned streamId, unsigned listIndex, unsigned flags,
                                         quint64 position, quint32 bytes);

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
    unsigned sendFileContents(unsigned streamId, bool ok, const QByteArray &data);

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

    // Files offered to the remote side (worker thread): the list it last asked for.
    QList<LocalFile> m_offered;
    QFile m_in;
    int m_inIndex = -1;

    QMutex m_lock;                               // guards the cached local content
    QString m_text;
    QByteArray m_dib;
    QList<LocalFile> m_localFiles;
    bool m_ignoreNextChange = false;             // GUI thread: our own setText/setImage/setUrls
};
