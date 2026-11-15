#include "rdp/RdpClipboard.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

#include <freerdp/channels/cliprdr.h>
#include <freerdp/client/cliprdr.h>
#include <winpr/user.h>

namespace {

// Largest clipboard payload accepted from the remote side.
constexpr quint32 kMaxClipboardBytes = 32u * 1024 * 1024;
// File contents are fetched in pieces of this size (servers may send less).
constexpr quint32 kFileChunk = 1024u * 1024;
// FILEDESCRIPTORW on the wire.
constexpr int kDescriptorBytes = 592;
constexpr quint32 kFdAttributes = 0x00000004, kFdWriteTime = 0x00000020, kFdFileSize = 0x00000040;
constexpr quint32 kAttrDirectory = 0x00000010;
const char kFileListFormat[] = "FileGroupDescriptorW";

RdpClipboard *self(CliprdrClientContext *ctx) { return ctx ? static_cast<RdpClipboard *>(ctx->custom) : nullptr; }

UINT monitorReady(CliprdrClientContext *ctx, const CLIPRDR_MONITOR_READY *)
{
    auto *c = self(ctx);
    return c ? c->onMonitorReady() : CHANNEL_RC_OK;
}

UINT serverCapabilities(CliprdrClientContext *ctx, const CLIPRDR_CAPABILITIES *caps)
{
    auto *c = self(ctx);
    if (!c || !caps || caps->cCapabilitiesSets < 1 || !caps->capabilitySets)
        return CHANNEL_RC_OK;
    // FreeRDP hands over the general set (the only one defined) as the first entry.
    if (caps->capabilitySets[0].capabilitySetType == CB_CAPSTYPE_GENERAL)
        c->onServerCapabilities(
            reinterpret_cast<const CLIPRDR_GENERAL_CAPABILITY_SET *>(caps->capabilitySets)->generalFlags);
    return CHANNEL_RC_OK;
}

UINT serverFormatList(CliprdrClientContext *ctx, const CLIPRDR_FORMAT_LIST *list)
{
    auto *c = self(ctx);
    if (!c || !list)
        return CHANNEL_RC_OK;
    QList<RdpClipboard::ServerFormat> formats;
    for (UINT32 i = 0; i < list->numFormats && list->formats && i < 512; ++i) {
        const char *name = list->formats[i].formatName;
        formats.append({ list->formats[i].formatId,
                         name ? QString::fromUtf8(name, qsizetype(qstrnlen(name, 256))) : QString() });
    }
    return c->onServerFormatList(formats);
}

UINT serverFormatListResponse(CliprdrClientContext *, const CLIPRDR_FORMAT_LIST_RESPONSE *) { return CHANNEL_RC_OK; }

UINT serverFormatDataRequest(CliprdrClientContext *ctx, const CLIPRDR_FORMAT_DATA_REQUEST *req)
{
    auto *c = self(ctx);
    return c && req ? c->onServerFormatDataRequest(req->requestedFormatId) : CHANNEL_RC_OK;
}

UINT serverFormatDataResponse(CliprdrClientContext *ctx, const CLIPRDR_FORMAT_DATA_RESPONSE *resp)
{
    auto *c = self(ctx);
    if (!c || !resp)
        return CHANNEL_RC_OK;
    const quint32 len = resp->common.dataLen;
    const bool ok = (resp->common.msgFlags & CB_RESPONSE_OK) && resp->requestedFormatData
        && len <= kMaxClipboardBytes;
    return c->onServerFormatDataResponse(
        ok, ok ? QByteArray(reinterpret_cast<const char *>(resp->requestedFormatData), qsizetype(len))
               : QByteArray());
}

UINT serverFileContentsResponse(CliprdrClientContext *ctx, const CLIPRDR_FILE_CONTENTS_RESPONSE *resp)
{
    auto *c = self(ctx);
    if (!c || !resp)
        return CHANNEL_RC_OK;
    const quint32 len = resp->cbRequested;
    const bool ok = (resp->common.msgFlags & CB_RESPONSE_OK) && (resp->requestedData || len == 0)
        && len <= kMaxClipboardBytes;
    return c->onServerFileContentsResponse(
        ok, resp->streamId,
        ok && len ? QByteArray(reinterpret_cast<const char *>(resp->requestedData), qsizetype(len))
                  : QByteArray());
}

QDateTime fromFileTime(quint64 ticks)
{
    // 100 ns ticks since 1601-01-01 UTC.
    constexpr qint64 kEpochDiffMs = 11644473600000LL;
    const qint64 ms = qint64(ticks / 10000) - kEpochDiffMs;
    return ticks ? QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC) : QDateTime();
}

} // namespace

RdpClipboard::Mode RdpClipboard::modeFromString(const QString &s)
{
    if (s == QLatin1String("to_remote"))
        return Mode::ToRemote;
    if (s == QLatin1String("to_local"))
        return Mode::ToLocal;
    if (s == QLatin1String("off"))
        return Mode::Off;
    return Mode::Both;
}

RdpClipboard::RdpClipboard(Mode mode, std::function<void(std::function<void()>)> post, QObject *parent)
    : QObject(parent)
    , m_mode(mode)
    , m_post(std::move(post))
{
    if (!enabled())
        return;
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &RdpClipboard::onLocalChanged);
    onLocalChanged();
}

RdpClipboard::~RdpClipboard()
{
    // The worker thread has stopped by now (the session joins it first).
    m_out.close();
    if (!m_sessionDir.isEmpty())
        QDir(m_sessionDir).removeRecursively();
    QMutexLocker lock(&m_lock);
    m_text.fill(QChar(0));
    m_dib.fill('\0');
}

void RdpClipboard::attach(CliprdrClientContext *ctx)
{
    m_ctx = ctx;
    ctx->custom = this;
    ctx->MonitorReady = monitorReady;
    ctx->ServerCapabilities = serverCapabilities;
    ctx->ServerFormatList = serverFormatList;
    ctx->ServerFormatListResponse = serverFormatListResponse;
    ctx->ServerFormatDataRequest = serverFormatDataRequest;
    ctx->ServerFormatDataResponse = serverFormatDataResponse;
    ctx->ServerFileContentsResponse = serverFileContentsResponse;
}

void RdpClipboard::detach()
{
    if (m_ctx) {
        m_ctx->MonitorReady = nullptr;
        m_ctx->ServerCapabilities = nullptr;
        m_ctx->ServerFormatList = nullptr;
        m_ctx->ServerFormatListResponse = nullptr;
        m_ctx->ServerFormatDataRequest = nullptr;
        m_ctx->ServerFormatDataResponse = nullptr;
        m_ctx->ServerFileContentsResponse = nullptr;
        m_ctx->custom = nullptr;
    }
    abortFiles();
    m_ctx = nullptr;
    m_ready = false;
    m_serverFiles = false;
}

// --- Local side (GUI thread) --------------------------------------------------

void RdpClipboard::onLocalChanged()
{
    if (m_ignoreNextChange) {
        m_ignoreNextChange = false;
        return;
    }
    if (!toRemote())
        return;
    const QMimeData *md = QGuiApplication::clipboard()->mimeData();
    QString text;
    QByteArray dib;
    if (md && md->hasImage())
        dib = imageToDib(qvariant_cast<QImage>(md->imageData()));
    if (md && md->hasText())
        text = md->text();
    {
        QMutexLocker lock(&m_lock);
        m_text = text;
        m_dib = dib;
    }
    m_post([this] { sendFormatList(); });
}

// --- Channel side (worker thread) ------------------------------------------------

unsigned RdpClipboard::onMonitorReady()
{
    CLIPRDR_GENERAL_CAPABILITY_SET general{};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES;
    // Files are taken from the remote clipboard as streams (no paths shared), never offered to it.
    if (toLocal())
        general.generalFlags |= CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS
                              | CB_HUGE_FILE_SUPPORT_ENABLED;
    CLIPRDR_CAPABILITIES caps{};
    caps.cCapabilitiesSets = 1;
    caps.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET *>(&general);
    const UINT rc = m_ctx->ClientCapabilities(m_ctx, &caps);
    if (rc != CHANNEL_RC_OK)
        return rc;
    m_ready = true;
    return sendFormatList();
}

unsigned RdpClipboard::sendFormatList()
{
    if (!m_ctx || !m_ready)
        return CHANNEL_RC_OK;
    CLIPRDR_FORMAT formats[2] = {};
    UINT32 n = 0;
    {
        QMutexLocker lock(&m_lock);
        if (toRemote() && !m_text.isEmpty())
            formats[n++].formatId = CF_UNICODETEXT;
        if (toRemote() && !m_dib.isEmpty())
            formats[n++].formatId = CF_DIB;
    }
    CLIPRDR_FORMAT_LIST list{};
    list.common.msgType = CB_FORMAT_LIST;
    list.numFormats = n;
    list.formats = formats;
    return m_ctx->ClientFormatList(m_ctx, &list);
}

void RdpClipboard::onServerCapabilities(unsigned generalFlags)
{
    m_serverFiles = (generalFlags & CB_STREAM_FILECLIP_ENABLED) != 0;
}

unsigned RdpClipboard::onServerFormatList(const QList<ServerFormat> &formats)
{
    CLIPRDR_FORMAT_LIST_RESPONSE resp{};
    resp.common.msgType = CB_FORMAT_LIST_RESPONSE;
    resp.common.msgFlags = CB_RESPONSE_OK;
    UINT rc = m_ctx->ClientFormatListResponse(m_ctx, &resp);
    // Something new was copied on the remote side: a running download is out of date.
    abortFiles();
    if (rc != CHANNEL_RC_OK || !toLocal())
        return rc;
    m_fileFormat = 0;
    bool text = false, dib = false;
    for (const ServerFormat &f : formats) {
        if (f.name == QLatin1String(kFileListFormat))
            m_fileFormat = f.id;
        text = text || f.id == CF_UNICODETEXT;
        dib = dib || f.id == CF_DIB;
    }
    // Eagerly fetch the best format so it is on the Mac clipboard right away. Files win: the text
    // that comes with them is just their names.
    m_requested = (m_fileFormat && m_serverFiles) ? m_fileFormat
                : text ? CF_UNICODETEXT : dib ? CF_DIB : 0;
    if (!m_requested)
        return CHANNEL_RC_OK;
    CLIPRDR_FORMAT_DATA_REQUEST req{};
    req.common.msgType = CB_FORMAT_DATA_REQUEST;
    req.requestedFormatId = m_requested;
    return m_ctx->ClientFormatDataRequest(m_ctx, &req);
}

unsigned RdpClipboard::onServerFormatDataResponse(bool ok, const QByteArray &data)
{
    const unsigned format = m_requested;
    m_requested = 0;
    if (!ok || data.isEmpty() || !toLocal())
        return CHANNEL_RC_OK;
    if (format && format == m_fileFormat) {
        QList<RemoteFile> files;
        if (!parseFileList(data, &files)) {
            emit notice(tr("Couldn't copy the files from the remote computer: their list was unreadable."));
            return CHANNEL_RC_OK;
        }
        startFiles(files);
        return nextFileStep();
    }
    if (format == CF_UNICODETEXT) {
        QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData()),
                                          data.size() / 2);
        text.truncate(text.indexOf(QChar(0)) >= 0 ? text.indexOf(QChar(0)) : text.size());
        QMetaObject::invokeMethod(this, [this, text] {
            m_ignoreNextChange = true;
            QGuiApplication::clipboard()->setText(text);
        }, Qt::QueuedConnection);
    } else if (format == CF_DIB) {
        const QImage img = dibToImage(data);
        if (!img.isNull())
            QMetaObject::invokeMethod(this, [this, img] {
                m_ignoreNextChange = true;
                QGuiApplication::clipboard()->setImage(img);
            }, Qt::QueuedConnection);
    }
    return CHANNEL_RC_OK;
}

unsigned RdpClipboard::onServerFormatDataRequest(unsigned format)
{
    QByteArray data;
    if (toRemote()) {
        QMutexLocker lock(&m_lock);
        if (format == CF_UNICODETEXT && !m_text.isEmpty()) {
            const std::u16string s = m_text.toStdU16String();
            data = QByteArray(reinterpret_cast<const char *>(s.c_str()), int((s.size() + 1) * 2));
        } else if (format == CF_DIB) {
            data = m_dib;
        }
    }
    CLIPRDR_FORMAT_DATA_RESPONSE resp{};
    resp.common.msgType = CB_FORMAT_DATA_RESPONSE;
    resp.common.msgFlags = data.isEmpty() ? CB_RESPONSE_FAIL : CB_RESPONSE_OK;
    resp.common.dataLen = UINT32(data.size());
    resp.requestedFormatData = reinterpret_cast<const BYTE *>(data.constData());
    const UINT rc = m_ctx->ClientFormatDataResponse(m_ctx, &resp);
    data.fill('\0');
    return rc;
}

// --- Files from the remote clipboard (worker thread) ------------------------------------

QString RdpClipboard::filesRoot() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
         + QLatin1String("/Pasted Files");
}

void RdpClipboard::startFiles(const QList<RemoteFile> &files)
{
    quint64 total = 0;
    for (const RemoteFile &f : files)
        total += f.dir ? 0 : f.size;
    if (files.isEmpty())
        return;
    if (total > kMaxFileBytes) {
        emit notice(tr("The copied files are larger than 2 GB. Copy fewer files at a time."));
        return;
    }
    if (m_sessionDir.isEmpty()) {
        // Leftovers of a session that didn't end cleanly (crash, power loss) go first.
        const QString root = filesRoot();
        QDir().mkpath(root);
        QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QDateTime stale = QDateTime::currentDateTimeUtc().addDays(-1);
        for (const QFileInfo &fi : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (fi.lastModified().toUTC() < stale)
                QDir(fi.absoluteFilePath()).removeRecursively();
        m_sessionDir = root + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    // A fresh folder per copy, so names from an earlier copy never mix in.
    if (!m_batchDir.isEmpty())
        QDir(m_batchDir).removeRecursively();
    m_batchDir = m_sessionDir + QLatin1Char('/') + QString::number(++m_batch);
    if (!QDir().mkpath(m_batchDir)) {
        failFiles(tr("Couldn't copy the files from the remote computer: the cache folder isn't writable. Check the free space on this Mac."));
        return;
    }
    m_files = files;
    m_fileIndex = -1;
    m_offset = 0;
    m_total = total;
    emit notice(tr("Copying files from the remote computer…"));
    // nextFileStep() opens the first file.
}

unsigned RdpClipboard::requestContents(unsigned flags, quint64 position, quint32 bytes)
{
    CLIPRDR_FILE_CONTENTS_REQUEST req{};
    req.common.msgType = CB_FILECONTENTS_REQUEST;
    req.streamId = ++m_streamId;
    req.listIndex = UINT32(m_fileIndex);
    req.dwFlags = flags;
    req.nPositionLow = UINT32(position & 0xffffffffu);
    req.nPositionHigh = UINT32(position >> 32);
    req.cbRequested = bytes;
    m_sizeRequest = (flags == FILECONTENTS_SIZE);
    return m_ctx->ClientFileContentsRequest(m_ctx, &req);
}

unsigned RdpClipboard::nextFileStep()
{
    if (m_files.isEmpty() || !m_ctx)
        return CHANNEL_RC_OK;
    // Continue the open file, or move on to the next one.
    if (m_out.isOpen()) {
        const RemoteFile &f = m_files[m_fileIndex];
        if (m_offset < f.size)
            return requestContents(FILECONTENTS_RANGE, m_offset,
                                   quint32(qMin<quint64>(kFileChunk, f.size - m_offset)));
        m_out.close();
        if (f.modified.isValid())
            QFile(m_batchDir + QLatin1Char('/') + f.path).setFileTime(f.modified, QFileDevice::FileModificationTime);
    }
    while (++m_fileIndex < m_files.size()) {
        RemoteFile &f = m_files[m_fileIndex];
        const QString target = m_batchDir + QLatin1Char('/') + f.path;
        if (f.dir) {
            if (!QDir().mkpath(target)) {
                failFiles(tr("Couldn't copy the files from the remote computer: a folder couldn't be created. Check the free space on this Mac."));
                return CHANNEL_RC_OK;
            }
            continue;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        m_out.setFileName(target);
        if (!m_out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failFiles(tr("Couldn't copy the files from the remote computer: a file couldn't be saved. Check the free space on this Mac."));
            return CHANNEL_RC_OK;
        }
        m_offset = 0;
        if (!f.hasSize)
            return requestContents(FILECONTENTS_SIZE, 0, 8);
        return nextFileStep();
    }
    finishFiles();
    return CHANNEL_RC_OK;
}

unsigned RdpClipboard::onServerFileContentsResponse(bool ok, unsigned streamId, const QByteArray &data)
{
    if (m_files.isEmpty() || streamId != m_streamId || m_fileIndex < 0 || m_fileIndex >= m_files.size())
        return CHANNEL_RC_OK;   // a late answer for a copy that was dropped
    RemoteFile &f = m_files[m_fileIndex];
    if (!ok) {
        failFiles(tr("Couldn't copy the files from the remote computer: it stopped sending them. Copy them again."));
        return CHANNEL_RC_OK;
    }
    if (m_sizeRequest) {
        if (data.size() < 8) {
            failFiles(tr("Couldn't copy the files from the remote computer: it stopped sending them. Copy them again."));
            return CHANNEL_RC_OK;
        }
        f.size = qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(data.constData()));
        f.hasSize = true;
        m_total += f.size;
        if (m_total > kMaxFileBytes) {
            failFiles(tr("The copied files are larger than 2 GB. Copy fewer files at a time."));
            return CHANNEL_RC_OK;
        }
        return nextFileStep();
    }
    // An empty piece before the end would loop forever; more than asked for is a broken server.
    const quint64 left = f.size - m_offset;
    if (data.isEmpty() || quint64(data.size()) > left
        || m_out.write(data) != data.size()) {
        failFiles(tr("Couldn't copy the files from the remote computer: it stopped sending them. Copy them again."));
        return CHANNEL_RC_OK;
    }
    m_offset += quint64(data.size());
    return nextFileStep();
}

void RdpClipboard::finishFiles()
{
    // Top-level entries go on the Mac clipboard; Finder pastes folders with their contents.
    QList<QUrl> urls;
    for (const RemoteFile &f : std::as_const(m_files))
        if (!f.path.contains(QLatin1Char('/')))
            urls.append(QUrl::fromLocalFile(m_batchDir + QLatin1Char('/') + f.path));
    m_files.clear();
    m_fileIndex = -1;
    if (urls.isEmpty())
        return;
    QMetaObject::invokeMethod(this, [this, urls] {
        auto *md = new QMimeData;
        md->setUrls(urls);
        m_ignoreNextChange = true;
        QGuiApplication::clipboard()->setMimeData(md);
        emit notice(tr("Files from the remote computer are ready to paste."));
    }, Qt::QueuedConnection);
}

void RdpClipboard::failFiles(const QString &message)
{
    abortFiles();
    emit notice(message);
}

void RdpClipboard::abortFiles()
{
    if (m_out.isOpen())
        m_out.close();
    if (!m_files.isEmpty() && !m_batchDir.isEmpty()) {
        QDir(m_batchDir).removeRecursively();
        m_batchDir.clear();
    }
    m_files.clear();
    m_fileIndex = -1;
    m_offset = 0;
    m_total = 0;
    ++m_streamId;   // late answers no longer match
}

QString RdpClipboard::safeRelativePath(const QString &remoteName)
{
    QString name = remoteName;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || (name.size() > 1 && name[1] == QLatin1Char(':')))
        return {};
    const QStringList parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    for (const QString &p : parts)
        if (p == QLatin1String(".") || p == QLatin1String("..") || p.contains(QChar(0)))
            return {};
    return parts.join(QLatin1Char('/'));
}

bool RdpClipboard::parseFileList(const QByteArray &data, QList<RemoteFile> *out)
{
    out->clear();
    if (data.size() < 4)
        return false;
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    const quint32 count = qFromLittleEndian<quint32>(p);
    if (count > quint32(kMaxFiles) || qint64(data.size()) < 4 + qint64(count) * kDescriptorBytes)
        return false;
    for (quint32 i = 0; i < count; ++i) {
        const uchar *d = p + 4 + qint64(i) * kDescriptorBytes;
        const quint32 flags = qFromLittleEndian<quint32>(d);
        const quint32 attrs = qFromLittleEndian<quint32>(d + 36);
        const quint64 writeTime = qFromLittleEndian<quint64>(d + 56);
        const quint32 sizeHigh = qFromLittleEndian<quint32>(d + 64);
        const quint32 sizeLow = qFromLittleEndian<quint32>(d + 68);
        char16_t name[260];
        for (int k = 0; k < 260; ++k)
            name[k] = qFromLittleEndian<quint16>(d + 72 + k * 2);
        int len = 0;
        while (len < 260 && name[len])
            ++len;
        RemoteFile f;
        f.path = safeRelativePath(QString::fromUtf16(name, len));
        if (f.path.isEmpty())
            return false;
        f.dir = (flags & kFdAttributes) && (attrs & kAttrDirectory);
        f.hasSize = f.dir || (flags & kFdFileSize);
        f.size = f.dir ? 0 : (quint64(sizeHigh) << 32) | sizeLow;
        if (flags & kFdWriteTime)
            f.modified = fromFileTime(writeTime);
        out->append(f);
    }
    return true;
}

// --- Converters ---------------------------------------------------------------------

QByteArray RdpClipboard::imageToDib(const QImage &image)
{
    if (image.isNull())
        return {};
    QByteArray bmp;
    QBuffer buf(&bmp);
    buf.open(QIODevice::WriteOnly);
    if (!image.convertToFormat(QImage::Format_RGB32).save(&buf, "BMP") || bmp.size() <= 14)
        return {};
    return bmp.mid(14); // drop BITMAPFILEHEADER; the rest is a packed DIB
}

QImage RdpClipboard::dibToImage(const QByteArray &dib)
{
    // Remote input: validate the header with 64-bit arithmetic before building a BMP around it.
    if (dib.size() < 40 || quint64(dib.size()) > kMaxClipboardBytes)
        return {};
    const auto *p = reinterpret_cast<const uchar *>(dib.constData());
    const quint32 headerSize = qFromLittleEndian<quint32>(p);
    const qint32 width = qFromLittleEndian<qint32>(p + 4);
    const qint32 height = qFromLittleEndian<qint32>(p + 8);
    const quint16 bitCount = qFromLittleEndian<quint16>(p + 14);
    const quint32 compression = qFromLittleEndian<quint32>(p + 16);
    quint32 colors = qFromLittleEndian<quint32>(p + 32);
    if (headerSize < 40 || headerSize > quint32(dib.size()))
        return {};
    if (bitCount != 1 && bitCount != 4 && bitCount != 8 && bitCount != 16 && bitCount != 24 && bitCount != 32)
        return {};
    if (compression != 0 /* BI_RGB */ && compression != 3 /* BI_BITFIELDS */)
        return {};   // no RLE/JPEG/PNG paths from remote data
    if (width <= 0 || width > 16384 || height == 0 || height < -16384 || height > 16384)
        return {};
    if (colors > 256)
        return {};
    if (colors == 0 && bitCount <= 8)
        colors = 1u << bitCount;
    const qint64 offset = qint64(14) + headerSize + qint64(colors) * 4
                        + ((compression == 3 && headerSize == 40) ? 12 : 0);
    const qint64 rowBytes = ((qint64(width) * bitCount + 31) / 32) * 4;
    const qint64 pixels = rowBytes * qAbs(qint64(height));
    if (offset < 54 || offset + pixels > qint64(14) + dib.size())
        return {};
    QByteArray bmp(14, '\0');
    bmp[0] = 'B';
    bmp[1] = 'M';
    qToLittleEndian<quint32>(quint32(14 + dib.size()), reinterpret_cast<uchar *>(bmp.data()) + 2);
    qToLittleEndian<quint32>(quint32(offset), reinterpret_cast<uchar *>(bmp.data()) + 10);
    bmp += dib;
    return QImage::fromData(bmp, "BMP");
}
