#include "rdp/RdpClipboard.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QtEndian>

#include <freerdp/channels/cliprdr.h>
#include <freerdp/client/cliprdr.h>
#include <winpr/user.h>

namespace {

// Largest clipboard payload accepted from the remote side.
constexpr quint32 kMaxClipboardBytes = 32u * 1024 * 1024;

RdpClipboard *self(CliprdrClientContext *ctx) { return ctx ? static_cast<RdpClipboard *>(ctx->custom) : nullptr; }

UINT monitorReady(CliprdrClientContext *ctx, const CLIPRDR_MONITOR_READY *)
{
    auto *c = self(ctx);
    return c ? c->onMonitorReady() : CHANNEL_RC_OK;
}

UINT serverCapabilities(CliprdrClientContext *, const CLIPRDR_CAPABILITIES *) { return CHANNEL_RC_OK; }

UINT serverFormatList(CliprdrClientContext *ctx, const CLIPRDR_FORMAT_LIST *list)
{
    auto *c = self(ctx);
    if (!c || !list)
        return CHANNEL_RC_OK;
    QList<unsigned> formats;
    for (UINT32 i = 0; i < list->numFormats && list->formats && i < 512; ++i)
        formats.append(list->formats[i].formatId);
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
        m_ctx->custom = nullptr;
    }
    m_ctx = nullptr;
    m_ready = false;
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

unsigned RdpClipboard::onServerFormatList(const QList<unsigned> &formats)
{
    CLIPRDR_FORMAT_LIST_RESPONSE resp{};
    resp.common.msgType = CB_FORMAT_LIST_RESPONSE;
    resp.common.msgFlags = CB_RESPONSE_OK;
    UINT rc = m_ctx->ClientFormatListResponse(m_ctx, &resp);
    if (rc != CHANNEL_RC_OK || !toLocal())
        return rc;
    // Eagerly fetch the best format so it is on the Mac clipboard right away.
    m_requested = formats.contains(CF_UNICODETEXT) ? CF_UNICODETEXT
                : formats.contains(CF_DIB) ? CF_DIB : 0;
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
