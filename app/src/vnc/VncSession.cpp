#include "vnc/VncSession.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QtEndian>

#define OPENSSL_SUPPRESS_DEPRECATED   // DES_* (VNC password auth uses single DES by design)
#include <openssl/bn.h>
#include <openssl/des.h>
#include <openssl/evp.h>

#include <zlib.h>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libnetcore.h>

namespace {

constexpr int kMaxSide = 16384;   // sanity limit for server-sent sizes

enum : quint8 { SecNone = 1, SecVnc = 2, SecApple = 30 };
enum : qint32 { EncRaw = 0, EncCopyRect = 1, EncZrle = 16, EncDesktopSize = -223, EncExtendedDesktopSize = -308 };
// Apple's metadata pseudo-encodings (RFB 003.889; measured by the zDesk project, MIT,
// github.com/andrewtheguy/zdesk docs/apple-vnc-889.md).
enum : qint32 { EncAppleAdaptive = 0x3f3, EncAppleCursorPos = 0x44c, EncAppleDisplayInfo = 0x44d, EncAppleRekey = 0x44f, EncAppleCursor = 0x450, EncAppleLayout = 0x451 };
constexpr int kRecordTrailer = 20;                      // SHA-1
constexpr int kRecordMaxBody = (65535 / 16) * 16 - 2 - kRecordTrailer;

void putU16(QByteArray &b, quint16 v) { b.append(char(v >> 8)).append(char(v & 0xff)); }
void putU32(QByteArray &b, quint32 v) { putU16(b, quint16(v >> 16)); putU16(b, quint16(v & 0xffff)); }

// Big-endian, left-padded to `len` bytes.
QByteArray bnBytes(const BIGNUM *n, int len)
{
    QByteArray out(len, '\0');
    BN_bn2binpad(n, reinterpret_cast<unsigned char *>(out.data()), len);
    return out;
}

} // namespace

VncSession::VncSession(Params params, QObject *parent)
    : RemoteSession(parent)
    , m_p(std::move(params))
{
    if (QGuiApplication::clipboard() && (m_p.clipboard == QLatin1String("both") || m_p.clipboard == QLatin1String("to_remote")))
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &VncSession::onLocalClipboard);
}

VncSession::~VncSession()
{
    m_stop = true;
    if (const int fd = m_fd.load(); fd >= 0)
        ::shutdown(fd, SHUT_RDWR);
    m_thread.quit();
    m_thread.wait();
    if (m_zrle) {
        inflateEnd(m_zrle);
        delete m_zrle;
    }
    EVP_CIPHER_CTX_free(m_inCtx);
    EVP_CIPHER_CTX_free(m_outCtx);
    memset(m_wrapKey, 0, sizeof m_wrapKey);
    m_p.password.fill(QChar(0));
}

QSize VncSession::desktopSize() const
{
    QMutexLocker lock(&m_frameLock);
    return m_frame.size();
}

// The server's screens (ExtendedDesktopSize) when it has several, as {x, y, width, height}.
QVariantList VncSession::monitors() const
{
    QMutexLocker lock(&m_frameLock);
    QVariantList out;
    if (m_screens.size() < 2)
        return out;
    for (const QRect &r : m_screens)
        out.append(QVariantMap{{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}});
    return out;
}

bool VncSession::multiMonitor() const
{
    QMutexLocker lock(&m_frameLock);
    return m_screens.size() > 1;
}

void VncSession::copyInto(QImage &dst, const QRect &part, const QPoint &at) const
{
    QElapsedTimer t;
    t.start();
    QMutexLocker lock(&m_frameLock);
    const QRect src = part.intersected(m_frame.rect());
    const QRect fit = QRect(at, src.size()).intersected(dst.rect());
    if (src.isEmpty() || fit.isEmpty() || dst.format() != m_frame.format())
        return;
    const int w = fit.width(), dx = fit.x() - at.x(), dy = fit.y() - at.y();
    for (int row = 0; row < fit.height(); ++row)
        memcpy(reinterpret_cast<quint32 *>(dst.scanLine(fit.y() + row)) + fit.x(),
               reinterpret_cast<const quint32 *>(m_frame.constScanLine(src.y() + dy + row)) + src.x() + dx, size_t(w) * 4);
        m_statCopyNs += t.nsecsElapsed();
        ++m_statCopies;
}

QImage VncSession::frame() const
{
    QMutexLocker lock(&m_frameLock);
    return m_frame.copy();
}

QImage VncSession::frame(const QRect &part) const
{
    QElapsedTimer t;
    t.start();
    QMutexLocker lock(&m_frameLock);
    QImage out = m_frame.copy(part.intersected(m_frame.rect()));
    m_statCopyNs += t.nsecsElapsed();
    ++m_statCopies;
    return out;
}

void VncSession::start()
{
    connect(&m_thread, &QThread::started, this, [this] { run(); }, Qt::DirectConnection);
    m_thread.start();
}

void VncSession::close()
{
    if (m_state == QLatin1String("closed"))
        return;
    releaseKeys();
    if (m_p.lockOnDisconnect && m_state == QLatin1String("connected")) {
        // Control+Command+Q locks a Mac (Command is Super). Sent before the socket closes.
        const quint32 ctrl = 0xffe3, cmd = 0xffeb, q = 'q';
        sendKeysym(ctrl, true);
        sendKeysym(cmd, true);
        sendKeysym(q, true);
        sendKeysym(q, false);
        sendKeysym(cmd, false);
        sendKeysym(ctrl, false);
        QThread::msleep(300);   // let the keys leave before the connection does
    }
    m_stop = true;
    if (const int fd = m_fd.load(); fd >= 0)
        ::shutdown(fd, SHUT_RDWR);   // wakes the reader
}

void VncSession::setState(const QString &state, const QString &error)
{
    QMetaObject::invokeMethod(this, [this, state, error] {
        m_state = state;
        m_error = error;
        if (state == QLatin1String("connected"))
            m_wasConnected = true;
        emit stateChanged();
        if (state == QLatin1String("closed"))
            emit closed(this);
    }, Qt::QueuedConnection);
}

void VncSession::setPhase(const QString &phase)
{
    QMetaObject::invokeMethod(this, [this, phase] {
        if (m_phase == phase)
            return;
        m_phase = phase;
        emit phaseChanged();
    }, Qt::QueuedConnection);
}

// --- Socket ---------------------------------------------------------------------

bool VncSession::readExact(void *buf, size_t n)
{
    if (!m_records)
        return rawRead(buf, n);
    auto *p = static_cast<char *>(buf);
    while (n > 0) {
        if (m_inPos >= m_inBuf.size()) {
            m_inBuf.clear();
            m_inPos = 0;
            if (!fillRecord())
                return false;
            continue;
        }
        const size_t take = qMin<size_t>(n, size_t(m_inBuf.size() - m_inPos));
        memcpy(p, m_inBuf.constData() + m_inPos, take);
        m_inPos += qsizetype(take);
        p += take;
        n -= take;
    }
    return true;
}

bool VncSession::rawRead(void *buf, size_t n)
{
    auto *p = static_cast<char *>(buf);
    while (n > 0) {
        if (m_stop)
            return false;
        pollfd pfd{m_fd.load(), POLLIN, 0};
        const int r = ::poll(&pfd, 1, 200);
        if (r < 0)
            return false;
        if (r == 0)
            continue;
        const ssize_t got = ::recv(pfd.fd, p, n, 0);
        if (got <= 0)
            return false;
        m_statBytes += quint64(got);
        p += got;
        n -= size_t(got);
    }
    return true;
}

bool VncSession::writeAll(const void *buf, size_t n)
{
    QMutexLocker lock(&m_writeLock);
    if (m_writeBroken)
        return false;
    if (!m_records)
        return rawWrite(buf, n);
    // One message in as many records as it needs: u16 length, then AES-CBC of
    // (u16 body length, body, zero filler, SHA-1 of the sequence number and all before it).
    const auto *msg = static_cast<const char *>(buf);
    size_t left = n;
    do {
        const int body = int(qMin<size_t>(left, size_t(kRecordMaxBody)));
        const int filler = (16 - (2 + body + kRecordTrailer) % 16) % 16;
        const int total = 2 + body + filler + kRecordTrailer;
        QByteArray plain;
        plain.reserve(total);
        putU16(plain, quint16(body));
        plain.append(msg, body);
        plain.append(filler, '\0');
        QByteArray seq(4, '\0');
        qToBigEndian(m_outSeq++, seq.data());
        plain.append(QCryptographicHash::hash(seq + plain, QCryptographicHash::Sha1));
        QByteArray rec(2 + total, '\0');
        qToBigEndian(quint16(total), rec.data());
        int outLen = 0;
        if (!EVP_EncryptUpdate(m_outCtx, reinterpret_cast<uchar *>(rec.data() + 2), &outLen,
                               reinterpret_cast<const uchar *>(plain.constData()), total) || outLen != total
            || !rawWrite(rec.constData(), size_t(rec.size()))) {
            failRecords();   // the chain and counter moved past what was sent: nothing later can be read
            return false;
        }
        msg += body;
        left -= size_t(body);
    } while (left > 0);
    return true;
}

void VncSession::failRecords()
{
    // Records stay on (nothing may go out in cleartext); the socket is closed so the reader ends the
    // session as lost, and later writes are refused.
    m_writeBroken = true;
    if (const int fd = m_fd.load(); fd >= 0)
        ::shutdown(fd, SHUT_RDWR);
}

bool VncSession::rawWrite(const void *buf, size_t n)
{
    const int fd = m_fd.load();
    if (fd < 0)
        return false;
    auto *p = static_cast<const char *>(buf);
    while (n > 0) {
        const ssize_t sent = ::send(fd, p, n, 0);
        if (sent <= 0)
            return false;
        p += sent;
        n -= size_t(sent);
    }
    return true;
}

// --- Connection -----------------------------------------------------------------

void VncSession::run()
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        setState(QStringLiteral("closed"), tr("Couldn't open a connection."));
        return;
    }
    const int on = 1;
    setsockopt(fds[0], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    QByteArray target = QStringLiteral("%1:%2").arg(m_p.host).arg(m_p.port).toUtf8();
    if (NetcoreConnectFD(m_p.tunnelHandle, target.data(), fds[1]) != 0) {
        ::close(fds[0]);
        setState(QStringLiteral("closed"), tr("Couldn't open a connection through the tunnel."));
        return;
    }
    m_fd = fds[0];

    const QString err = handshake();
    if (err.isEmpty()) {
        setPhase(QStringLiteral("connected"));
        setState(QStringLiteral("connected"));
        const bool ok = messageLoop();
        const bool userClosed = m_stop;
        qInfo("vnc: connection ended (%s)", userClosed ? "closed by the user" : ok ? "by the server" : "error");
        m_fd = -1;
        ::close(fds[0]);
        setState(QStringLiteral("closed"), ok || userClosed ? QString()
                 : m_integrityFailed ? tr("The connection to %1 was closed because data arrived damaged or altered.").arg(m_p.title)
                                     : tr("The connection to %1 was lost.").arg(m_p.title));
        return;
    }
    m_fd = -1;
    ::close(fds[0]);
    setState(QStringLiteral("closed"), m_stop ? QString() : err);
}

QString VncSession::handshake()
{
    const QString unreachable = tr("Couldn't reach %1. Check that Screen Sharing (or another VNC server) is on and the address is right.").arg(m_p.title);
    char version[12];
    if (!readExact(version, 12) || memcmp(version, "RFB ", 4) != 0)
        return unreachable;
    // A Mac (3.889) gets Apple's own revision when we can sign in with a user name: it is the one
    // that reports the Mac's displays. Everything else, or BURROW_VNC_STANDARD=1, gets RFB 3.8.
    m_apple = memcmp(version, "RFB 003.889", 11) == 0 && !m_p.username.isEmpty()
        && !qEnvironmentVariableIntValue("BURROW_VNC_STANDARD");
    if (!writeAll(m_apple ? "RFB 003.889\n" : "RFB 003.008\n", 12))
        return unreachable;

    setPhase(QStringLiteral("signing_in"));
    quint8 count = 0;
    if (!readExact(&count, 1))
        return unreachable;
    if (count == 0) {
        quint32 len = 0;
        if (!readExact(&len, 4))
            return unreachable;
        len = qMin<quint32>(qFromBigEndian(len), 4096);
        QByteArray reason(int(len), '\0');
        readExact(reason.data(), len);
        return tr("%1 refused the connection: %2").arg(m_p.title, QString::fromUtf8(reason));
    }
    QByteArray types(count, '\0');
    if (!readExact(types.data(), count))
        return unreachable;
    // Apple's sign-in when we have a user name, else a VNC password, else none.
    quint8 chosen = 0;
    if (types.contains(char(SecApple)) && !m_p.username.isEmpty())
        chosen = SecApple;
    else if (types.contains(char(SecVnc)) && !m_p.password.isEmpty())
        chosen = SecVnc;
    else if (types.contains(char(SecNone)) && !m_apple)
        chosen = SecNone;
    else if (types.contains(char(SecApple)))
        return tr("This Mac signs in with a user name. Enter the Mac's user name and password in the device settings.");
    else
        return tr("This VNC server asks for a sign-in method Burrow doesn't support yet.");
    // Apple's revision keys its record layer from the Apple sign-in; anything else can't start it.
    if (m_apple && chosen != SecApple)
        return tr("This Mac signs in with a user name. Enter the Mac's user name and password in the device settings.");
    if (!writeAll(&chosen, 1))
        return unreachable;

    QString authErr;
    if (chosen == SecApple)
        authErr = authApple();
    else if (chosen == SecVnc)
        authErr = authVnc();
    if (!authErr.isEmpty())
        return authErr;

    quint32 result = 1;
    if (!readExact(&result, 4))
        return unreachable;
    if (qFromBigEndian(result) != 0) {
        // Never retried automatically: a wrong password must not lock the account. Apple's revision
        // sends no reason; RFB 3.8 sends one.
        quint32 len = 0;
        if (!m_apple && readExact(&len, 4)) {
            len = qMin<quint32>(qFromBigEndian(len), 4096);
            QByteArray reason(int(len), '\0');
            readExact(reason.data(), len);
        }
        return tr("The user name or password for %1 is wrong.").arg(m_p.title);
    }

    setPhase(QStringLiteral("opening"));
    // Shared (don't disconnect other viewers); Apple's 0x80 asks for its extended ServerInit.
    const quint8 shared = m_apple ? 0x81 : 1;
    if (!writeAll(&shared, 1))
        return unreachable;
    uchar init[24];
    if (!readExact(init, 24))
        return unreachable;
    const int w = qFromBigEndian<quint16>(init), h = qFromBigEndian<quint16>(init + 2);
    quint32 nameLen = qMin<quint32>(qFromBigEndian<quint32>(init + 20), 4096);
    QByteArray name(int(nameLen), '\0');
    if (!readExact(name.data(), nameLen))
        return unreachable;
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide)
        return tr("%1 sent an invalid screen size.").arg(m_p.title);
    resizeFrame(w, h);
    if (m_apple) {
        const QString err = appleSetup();
        if (!err.isEmpty())
            return err;
    }

    // 32-bit true colour, little endian, 0x00RRGGBB: the same bytes as QImage::Format_RGB32.
    QByteArray pf;
    pf.append(char(0)).append(3, '\0');                       // SetPixelFormat + padding
    pf.append(char(32)).append(char(24)).append(char(0)).append(char(1));
    putU16(pf, 255); putU16(pf, 255); putU16(pf, 255);
    pf.append(char(16)).append(char(8)).append(char(0)).append(3, '\0');
    m_adaptiveOn = m_apple && !qEnvironmentVariableIntValue("BURROW_VNC_ZRLE");
    if (!writeAll(pf.constData(), size_t(pf.size())) || !sendEncodings())
        return unreachable;
    requestUpdate(false);
    return {};
}

// Apple's prelude, in cleartext: who we are, control mode, then turn the record layer on. The Mac
// answers with the rekey; everything after it travels in records.
QString VncSession::appleSetup()
{
    const QString failed = tr("Couldn't open the encrypted session with %1.").arg(m_p.title);
    // ViewerInfo (0x21): 62 bytes of numbers - viewer class 1, application 2 (Screen Sharing)
    // version 6.1.0, macOS 26.6.2, then the server message types this viewer handles.
    uchar vi[66] = {};
    vi[0] = 0x21;
    qToBigEndian<quint16>(62, vi + 2);
    qToBigEndian<quint16>(1, vi + 4);
    qToBigEndian<quint32>(2, vi + 6);
    qToBigEndian<quint32>(6, vi + 10);
    qToBigEndian<quint32>(1, vi + 14);
    qToBigEndian<quint32>(26, vi + 22);
    qToBigEndian<quint32>(6, vi + 26);
    qToBigEndian<quint32>(2, vi + 30);
    vi[34] = 0xb0; vi[36] = 0x0c; vi[37] = 0x03; vi[38] = 0x90; vi[44] = 0x40;
    const uchar setMode[4] = {0x0a, 0, 0, 1};                                    // control
    const uchar encStart[12] = {0x12, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1};          // rekey, AES-128
    const uchar encInbound[8] = {0x12, 0, 0, 2, 0, 1, 0, 0};                     // decrypt what we send
    if (!writeAll(vi, sizeof vi) || !writeAll(setMode, 4) || !writeAll(encStart, 12) || !writeAll(encInbound, 8))
        return failed;
    // Only Bell and MiscStatus may come before the rekey.
    for (;;) {
        quint8 type = 0;
        if (!readExact(&type, 1))
            return failed;
        if (type == 2)
            continue;
        if (type == 0x14) {
            uchar h[3];
            if (!readExact(h, 3))
                return failed;
            QByteArray skip(qFromBigEndian<quint16>(h + 1), '\0');
            if (!readExact(skip.data(), size_t(skip.size())))
                return failed;
            continue;
        }
        if (type != 0) {
            qWarning("vnc: Mac sent message %u before the encrypted session", type);
            return failed;
        }
        uchar h[3];
        if (!readExact(h, 3))
            return failed;
        if (qFromBigEndian<quint16>(h + 1) == 0)
            continue;
        uchar rect[12];
        if (!readExact(rect, 12) || qFromBigEndian<qint32>(rect + 8) != EncAppleRekey
            || qFromBigEndian<quint16>(h + 1) != 1) {
            qWarning("vnc: Mac sent something other than the rekey first");
            return failed;
        }
        uchar body[36];
        if (!readExact(body, 36) || !startRecords(body))
            return failed;
        qInfo("vnc: Apple encrypted session up");
        return {};
    }
}

// The rekey: a u32 generation, then the key and the IV, each one AES-128 block (ECB) under the key
// from sign-in. Both directions start from them; each keeps its own CBC chain and counter.
bool VncSession::startRecords(const uchar *body)
{
    if (!m_wrapKeyValid)
        return false;
    m_wrapKeyValid = false;
    uchar keys[32];
    int out = 0;
    EVP_CIPHER_CTX *ecb = EVP_CIPHER_CTX_new();
    bool ok = ecb && EVP_DecryptInit_ex(ecb, EVP_aes_128_ecb(), nullptr, m_wrapKey, nullptr)
        && EVP_CIPHER_CTX_set_padding(ecb, 0) && EVP_DecryptUpdate(ecb, keys, &out, body + 4, 32) && out == 32;
    EVP_CIPHER_CTX_free(ecb);
    memset(m_wrapKey, 0, sizeof m_wrapKey);
    if (ok) {
        m_inCtx = EVP_CIPHER_CTX_new();
        m_outCtx = EVP_CIPHER_CTX_new();
        ok = m_inCtx && m_outCtx
            && EVP_DecryptInit_ex(m_inCtx, EVP_aes_128_cbc(), nullptr, keys, keys + 16) && EVP_CIPHER_CTX_set_padding(m_inCtx, 0)
            && EVP_EncryptInit_ex(m_outCtx, EVP_aes_128_cbc(), nullptr, keys, keys + 16) && EVP_CIPHER_CTX_set_padding(m_outCtx, 0);
    }
    memset(keys, 0, sizeof keys);
    if (ok) {
        QMutexLocker lock(&m_writeLock);
        m_records = true;
    }
    return ok;
}

// One record off the wire: decrypt, check its SHA-1, keep its body for readExact.
bool VncSession::fillRecord()
{
    uchar lb[2];
    if (!rawRead(lb, 2))
        return false;
    const int len = qFromBigEndian<quint16>(lb);
    if (len < 32 || len % 16 != 0)
        return false;
    QByteArray cipher(len, '\0'), plain(len, '\0');
    if (!rawRead(cipher.data(), size_t(len)))
        return false;
    int out = 0;
    if (!EVP_DecryptUpdate(m_inCtx, reinterpret_cast<uchar *>(plain.data()), &out,
                           reinterpret_cast<const uchar *>(cipher.constData()), len) || out != len)
        return false;
    QByteArray seq(4, '\0');
    qToBigEndian(m_inSeq++, seq.data());
    if (QCryptographicHash::hash(seq + plain.left(len - kRecordTrailer), QCryptographicHash::Sha1) != plain.right(kRecordTrailer)) {
        qWarning("vnc: a record from the Mac failed its integrity check");
        m_integrityFailed = true;
        return false;
    }
    const int body = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(plain.constData()));
    if (2 + body + kRecordTrailer > len)
        return false;
    m_inBuf = plain.mid(2, body);
    m_inPos = 0;
    return true;
}

// Apple's pointer shapes: a u32 cache id and a u32 compressed length. A length of 0 selects a
// stored shape; otherwise its own zlib stream holds w*h BGRA pixels, then a separate w*h alpha plane.
bool VncSession::readAppleCursor(int hx, int hy, int w, int h)
{
    uchar head[8];
    if (!readExact(head, 8))
        return false;
    const quint32 id = qFromBigEndian<quint32>(head), len = qFromBigEndian<quint32>(head + 4);
    if (len > 1024 * 1024)
        return false;
    QByteArray deflated(int(len), '\0');
    if (!readExact(deflated.data(), len))
        return false;
    CursorShape shape;
    if (len == 0) {
        if (!m_cursorCache.contains(id))
            return true;   // never stored: keep the current shape
        shape = m_cursorCache.value(id);
    } else {
        if (w <= 0 || h <= 0 || w > 256 || h > 256)
            return true;   // a bad shape is skipped; its bytes are already consumed
        const qsizetype pixels = qsizetype(w) * h;
        QByteArray raw(pixels * 5, '\0');
        uLongf outLen = uLongf(raw.size());
        if (uncompress(reinterpret_cast<Bytef *>(raw.data()), &outLen,
                       reinterpret_cast<const Bytef *>(deflated.constData()), uLong(deflated.size())) != Z_OK
            || qsizetype(outLen) != raw.size())
            return true;
        QImage img(w, h, QImage::Format_ARGB32);
        const auto *bgrx = reinterpret_cast<const uchar *>(raw.constData());
        const auto *alpha = bgrx + pixels * 4;
        for (int y = 0; y < h; ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const qsizetype i = qsizetype(y) * w + x;
                line[x] = qRgba(bgrx[i * 4 + 2], bgrx[i * 4 + 1], bgrx[i * 4], alpha[i]);
            }
        }
        shape = {img, QPoint(qBound(0, hx, w - 1), qBound(0, hy, h - 1))};
        qDebug("vnc: pointer shape %dx%d stored as %u", w, h, id);
        if (m_cursorCache.size() < 256)
            m_cursorCache.insert(id, shape);
    }
    QMetaObject::invokeMethod(this, [this, shape] {
        m_cursor = shape.image;
        m_cursorHot = shape.hotspot;
        emit cursorChanged();
    }, Qt::QueuedConnection);
    return true;
}

// AppleDisplayLayout: a u16 counting what follows, a 0x14-byte header (framebuffer size at 0x06,
// the display count at 0x12), then 0x38 bytes per display with its framebuffer edges at 0x1c
// (top, left, bottom, right). A layout also stops the Mac pushing, so updates are armed again.
bool VncSession::readAppleLayout()
{
    uchar lb[2];
    if (!readExact(lb, 2))
        return false;
    const int len = qFromBigEndian<quint16>(lb);
    QByteArray p(len, '\0');
    if (!readExact(p.data(), size_t(len)))
        return false;
    const auto *b = reinterpret_cast<const uchar *>(p.constData());
    if (len < 0x14)
        return false;
    const int fbW = qFromBigEndian<quint16>(b + 0x06), fbH = qFromBigEndian<quint16>(b + 0x08);
    const int count = qFromBigEndian<quint16>(b + 0x12);
    if (count < 1 || count > 25 || len < 0x14 + count * 0x38 || fbW <= 0 || fbH <= 0 || fbW > kMaxSide || fbH > kMaxSide)
        return false;
    QList<QRect> rects;
    QList<QPoint> origins;
    for (int i = 0; i < count; ++i) {
        const uchar *r = b + 0x14 + i * 0x38;
        const QPoint origin(qint16(qFromBigEndian<quint16>(r + 0x16)), qint16(qFromBigEndian<quint16>(r + 0x14)));
        const quint32 flags = qFromBigEndian<quint32>(r + 0x24);
        if ((flags & 0x02) && origins.contains(origin))
            continue;   // another member of a mirror set: the same picture
        origins.append(origin);
        const int top = qint16(qFromBigEndian<quint16>(r + 0x1c)), left = qint16(qFromBigEndian<quint16>(r + 0x1e));
        const int bottom = qint16(qFromBigEndian<quint16>(r + 0x20)), right = qint16(qFromBigEndian<quint16>(r + 0x22));
        if (right > left && bottom > top)
            rects.append(QRect(left, top, right - left, bottom - top));
    }
    // Framebuffer coordinates start at the displays' top-left corner.
    QRect box;
    for (const QRect &r : std::as_const(rects))
        box = box.united(r);
    const QRect fb(0, 0, fbW, fbH);
    QList<QRect> screens;
    for (const QRect &r : std::as_const(rects))
        if (fb.contains(r.translated(-box.topLeft())))
            screens.append(r.translated(-box.topLeft()));
    QStringList desc;
    for (const QRect &r : std::as_const(screens))
        desc << QStringLiteral("%1x%2+%3+%4").arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y());
    m_screensReported = true;
    qInfo("vnc: Mac displays %dx%d: %s", fbW, fbH, qPrintable(desc.join(QStringLiteral(", "))));
    if (desktopSize() != QSize(fbW, fbH))
        resizeFrame(fbW, fbH);
    {
        QMutexLocker lock(&m_frameLock);
        m_screens = screens.size() > 1 ? screens : QList<QRect>{};
    }
    QMetaObject::invokeMethod(this, [this] { emit desktopSizeChanged(); }, Qt::QueuedConnection);
    // AutoFrameBufferUpdate (0x09): push every change, interval 0, for the whole framebuffer.
    QByteArray arm;
    arm.append(char(0x09)).append(char(0));
    putU16(arm, 1);
    putU32(arm, 0);
    putU16(arm, 0); putU16(arm, 0); putU16(arm, quint16(fbW)); putU16(arm, quint16(fbH));
    m_armed = writeAll(arm.constData(), size_t(arm.size()));
    return true;
}

// Every encoding listed must be readable: the server takes the list as a promise. A Mac reports
// its displays only when both DisplayInfo and the layout are listed, and codes every rectangle in the
// first of its codecs listed: Adaptive (0x3f3) when on, else ZRLE.
bool VncSession::sendEncodings()
{
    QList<qint32> encodings;
    if (m_apple) {
        encodings = {EncRaw, EncAppleCursorPos, EncAppleDisplayInfo, EncAppleRekey, EncAppleCursor, EncAppleLayout, EncDesktopSize};
        if (m_adaptiveOn)
            encodings << EncAppleAdaptive;
        encodings << EncZrle;
    } else {
        encodings = {EncZrle, EncCopyRect, EncRaw, EncExtendedDesktopSize, EncDesktopSize};
    }
    QByteArray enc;
    enc.append(char(2)).append(char(0));
    putU16(enc, quint16(encodings.size()));
    for (qint32 e : encodings)
        putU32(enc, quint32(e));
    return writeAll(enc.constData(), size_t(enc.size()));
}

// Apple's Adaptive picture: a u32 length, then a body the decoder draws onto the framebuffer.
bool VncSession::readAdaptive(int x, int y, int w, int h)
{
    quint32 len = 0;
    if (!readExact(&len, 4))
        return false;
    len = qFromBigEndian(len);
    if (len > 64 * 1024 * 1024)
        return false;
    QByteArray body(int(len), '\0');
    if (!readExact(body.data(), len))
        return false;
    QElapsedTimer cpu;
    cpu.start();
    bool drew = false;
    QString error;
    bool ok;
    {
        QMutexLocker lock(&m_frameLock);
        ok = m_adaptive.decode(m_frame, x, y, w, h, body, &drew, &error);
    }
    m_statCpuNs += cpu.nsecsElapsed();
    if (!ok && m_adaptiveOn) {
        // The decoder's state no longer matches the Mac's: go back to ZRLE and repaint everything.
        // (Rectangles still in flight in Adaptive are skipped.)
        qWarning("vnc: an Adaptive rectangle didn't decode (%s); switching to ZRLE", qPrintable(error));
        m_adaptiveOn = false;
        if (!sendEncodings())
            return false;
        requestUpdate(false);
    }
    return true;
}

// Apple Remote Desktop sign-in (type 30): Diffie-Hellman, then the user name and password encrypted
// with AES-128 under MD5 of the shared secret.
QString VncSession::authApple()
{
    const QString failed = tr("Couldn't sign in to %1.").arg(m_p.title);
    uchar head[4];
    if (!readExact(head, 4))
        return failed;
    const quint16 generator = qFromBigEndian<quint16>(head), keyLen = qFromBigEndian<quint16>(head + 2);
    if (keyLen < 16 || keyLen > 1024)
        return failed;
    QByteArray prime(keyLen, '\0'), serverPub(keyLen, '\0');
    if (!readExact(prime.data(), keyLen) || !readExact(serverPub.data(), keyLen))
        return failed;

    BN_CTX *ctx = BN_CTX_new();
    BIGNUM *p = BN_bin2bn(reinterpret_cast<const uchar *>(prime.constData()), keyLen, nullptr);
    BIGNUM *g = BN_new();
    BN_set_word(g, generator);
    BIGNUM *y = BN_bin2bn(reinterpret_cast<const uchar *>(serverPub.constData()), keyLen, nullptr);
    BIGNUM *priv = BN_new(), *pub = BN_new(), *secret = BN_new();
    // The server's public value must be in (1, p-1): 0, 1 and p-1 give a secret anyone knows.
    BIGNUM *pMinus1 = p ? BN_dup(p) : nullptr;
    const bool yOk = pMinus1 && y && BN_sub_word(pMinus1, 1) && BN_cmp(y, BN_value_one()) > 0 && BN_cmp(y, pMinus1) < 0
        && generator >= 2 && BN_num_bytes(p) == keyLen;
    BN_free(pMinus1);
    bool ok = yOk && ctx && p && g && y && priv && pub && secret
        && BN_rand(priv, keyLen * 8 - 1, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY)
        && BN_mod_exp(pub, g, priv, p, ctx) && BN_mod_exp(secret, y, priv, p, ctx);
    QByteArray pubBytes, secretBytes;
    if (ok) {
        pubBytes = bnBytes(pub, keyLen);
        secretBytes = bnBytes(secret, keyLen);
    }
    BN_clear_free(priv);
    BN_clear_free(secret);
    BN_free(pub); BN_free(y); BN_free(g); BN_free(p);
    BN_CTX_free(ctx);
    if (!ok)
        return failed;

    uchar key[16];
    unsigned keyOut = 0;
    ok = EVP_Digest(secretBytes.constData(), size_t(secretBytes.size()), key, &keyOut, EVP_md5(), nullptr) && keyOut == 16;
    secretBytes.fill('\0');
    if (ok) {
        memcpy(m_wrapKey, key, 16);   // also wraps the record layer's first keys (Apple's rekey)
        m_wrapKeyValid = true;
    }

    // 64 bytes user name + 64 bytes password, each NUL-terminated, the rest random.
    uchar creds[128];
    for (uchar &c : creds)
        c = uchar(QRandomGenerator::system()->generate());
    QByteArray user = m_p.username.toUtf8().left(63), pass = m_p.password.toUtf8().left(63);
    memcpy(creds, user.constData(), size_t(user.size()));
    creds[user.size()] = 0;
    memcpy(creds + 64, pass.constData(), size_t(pass.size()));
    creds[64 + pass.size()] = 0;
    pass.fill('\0');

    uchar cipher[128];
    int outLen = 0, finLen = 0;
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    ok = ok && c && EVP_EncryptInit_ex(c, EVP_aes_128_ecb(), nullptr, key, nullptr)
        && EVP_CIPHER_CTX_set_padding(c, 0) && EVP_EncryptUpdate(c, cipher, &outLen, creds, 128)
        && EVP_EncryptFinal_ex(c, cipher + outLen, &finLen) && outLen + finLen == 128;
    EVP_CIPHER_CTX_free(c);
    memset(creds, 0, sizeof creds);
    memset(key, 0, sizeof key);
    if (!ok)
        return failed;
    if (!writeAll(cipher, 128) || !writeAll(pubBytes.constData(), size_t(pubBytes.size())))
        return failed;
    return {};
}

QByteArray VncSession::vncAuthResponse(const QByteArray &challenge, const QString &password)
{
    // The password (8 bytes at most, zero padded) is the DES key with each byte's bits reversed.
    DES_cblock key{};
    const QByteArray pw = password.toLatin1().left(8);
    for (int i = 0; i < pw.size(); ++i) {
        uchar b = uchar(pw.at(i)), r = 0;
        for (int bit = 0; bit < 8; ++bit)
            if (b & (1 << bit))
                r |= uchar(0x80 >> bit);
        key[i] = r;
    }
    DES_key_schedule ks;
    DES_set_key_unchecked(&key, &ks);
    QByteArray out(16, '\0');
    for (int block = 0; block < 2 && challenge.size() >= 16; ++block) {
        DES_cblock in, enc;
        memcpy(in, challenge.constData() + block * 8, 8);
        DES_ecb_encrypt(&in, &enc, &ks, DES_ENCRYPT);
        memcpy(out.data() + block * 8, enc, 8);
    }
    memset(&ks, 0, sizeof ks);
    memset(key, 0, sizeof key);
    return out;
}

QString VncSession::authVnc()
{
    QByteArray challenge(16, '\0');
    if (!readExact(challenge.data(), 16))
        return tr("Couldn't sign in to %1.").arg(m_p.title);
    QByteArray response = vncAuthResponse(challenge, m_p.password);
    const bool ok = writeAll(response.constData(), 16);
    response.fill('\0');
    return ok ? QString() : tr("Couldn't sign in to %1.").arg(m_p.title);
}

// --- Pictures -------------------------------------------------------------------

void VncSession::resizeFrame(int w, int h)
{
    m_adaptive.resize(w, h);   // worker thread, like every decode
    {
        QMutexLocker lock(&m_frameLock);
        m_frame = QImage(w, h, QImage::Format_RGB32);
        m_frame.fill(Qt::black);
    }
    QMetaObject::invokeMethod(this, [this] { emit desktopSizeChanged(); }, Qt::QueuedConnection);
}

void VncSession::requestUpdate(bool incremental)
{
    const QSize s = desktopSize();
    QByteArray m;
    m.append(char(3)).append(char(incremental ? 1 : 0));
    putU16(m, 0); putU16(m, 0);
    putU16(m, quint16(s.width())); putU16(m, quint16(s.height()));
    writeAll(m.constData(), size_t(m.size()));
}

bool VncSession::messageLoop()
{
    while (!m_stop) {
        quint8 type = 0;
        if (!readExact(&type, 1))
            return m_stop;
        switch (type) {
        case 0:   // FramebufferUpdate
            if (!readFramebufferUpdate())
                return m_stop;
            if (!m_armed)   // a Mac pushes updates once armed (see readAppleLayout)
                requestUpdate(true);
            break;
        case 0x14: {   // Apple's MiscStatus: pad, u16 length, body (status we don't act on)
            if (!m_apple)
                return false;
            uchar h[3];
            if (!readExact(h, 3))
                return m_stop;
            QByteArray skip(qFromBigEndian<quint16>(h + 1), '\0');
            if (!readExact(skip.data(), size_t(skip.size())))
                return m_stop;
            break;
        }
        case 1: { // SetColourMapEntries (unused with true colour)
            uchar h[5];
            if (!readExact(h, 5))
                return m_stop;
            QByteArray skip(qFromBigEndian<quint16>(h + 3) * 6, '\0');
            if (!readExact(skip.data(), size_t(skip.size())))
                return m_stop;
            break;
        }
        case 2:   // Bell
            break;
        case 3: { // ServerCutText (Latin-1)
            uchar h[7];
            if (!readExact(h, 7))
                return m_stop;
            const quint32 len = qFromBigEndian<quint32>(h + 3);
            if (len > 10 * 1024 * 1024)
                return false;
            QByteArray text(int(len), '\0');
            if (!readExact(text.data(), len))
                return m_stop;
            if (m_p.clipboard == QLatin1String("both") || m_p.clipboard == QLatin1String("to_local")) {
                const QString s = QString::fromLatin1(text);
                QMetaObject::invokeMethod(this, [this, s] {
                    m_lastClipboard = s;
                    QGuiApplication::clipboard()->setText(s);
                }, Qt::QueuedConnection);
            }
            break;
        }
        default:
            qWarning("vnc: unknown server message %u", type);
            return false;
        }
    }
    return true;
}

bool VncSession::readFramebufferUpdate()
{
    uchar h[3];
    if (!readExact(h, 3))
        return false;
    const int rects = qFromBigEndian<quint16>(h + 1);
    QRect dirty;
    for (int i = 0; i < rects; ++i) {
        uchar r[12];
        if (!readExact(r, 12))
            return false;
        const int x = qFromBigEndian<quint16>(r), y = qFromBigEndian<quint16>(r + 2);
        const int w = qFromBigEndian<quint16>(r + 4), hh = qFromBigEndian<quint16>(r + 6);
        const qint32 enc = qFromBigEndian<qint32>(r + 8);
        if (m_apple) {
            if (enc == EncAppleCursorPos)
                continue;   // the pointer's position, in the rectangle header only
            if (enc == EncAppleDisplayInfo) {   // the older display list: stepped over
                uchar head[10];
                if (!readExact(head, 10))
                    return false;
                QByteArray skip(qFromBigEndian<quint16>(head + 8) * 0x1c, '\0');
                if (!readExact(skip.data(), size_t(skip.size())))
                    return false;
                continue;
            }
            if (enc == EncAppleLayout) {
                if (!readAppleLayout())
                    return false;
                continue;
            }
            if (enc == EncAppleAdaptive) {
                if (!readAdaptive(x, y, w, hh))
                    return false;
                m_statPixels += quint64(w) * quint64(hh);
                dirty = dirty.united(QRect(x, y, w, hh));
                continue;
            }
            if (enc == EncAppleCursor) {   // the header carries the hotspot and the shape's size
                if (!readAppleCursor(x, y, w, hh))
                    return false;
                continue;
            }
            if (enc == EncAppleRekey) {
                qWarning("vnc: the Mac changed keys mid-session, which Burrow never asks for");
                return false;
            }
        }
        if (enc == EncExtendedDesktopSize) {
            if (!readExtendedDesktopSize(w, hh))
                return false;
            dirty = QRect(0, 0, w, hh);
            continue;
        }
        if (enc == EncDesktopSize) {
            if (w <= 0 || hh <= 0 || w > kMaxSide || hh > kMaxSide)
                return false;
            resizeFrame(w, hh);
            dirty = QRect(0, 0, w, hh);
            continue;
        }
        const QSize fb = desktopSize();
        if (x + w > fb.width() || y + hh > fb.height()) {
            qWarning("vnc: rectangle outside the screen");
            return false;   // a broken or hostile server
        }
        bool ok = false;
        QElapsedTimer decode;
        decode.start();
        switch (enc) {
        case EncRaw: ok = readRaw(x, y, w, hh); break;
        case EncCopyRect: ok = readCopyRect(x, y, w, hh); break;
        case EncZrle: ok = readZrle(x, y, w, hh); break;
        default: qWarning("vnc: unexpected encoding %d", enc); return false;
        }
        if (!ok) {
            if (!m_stop)   // closing mid-rectangle isn't a decoding error
                qWarning("vnc: couldn't read a %dx%d rectangle (encoding %d)", w, hh, enc);
            return false;
        }
        m_statDecodeNs += decode.nsecsElapsed();
        m_statPixels += quint64(w) * quint64(hh);
        dirty = dirty.united(QRect(x, y, w, hh));
    }
    ++m_statUpdates;
    logStats();
    if (m_firstUpdate) {
        m_firstUpdate = false;
        if (!m_screensReported)
            qInfo("vnc: the server doesn't report its screens (no ExtendedDesktopSize): one picture");
    }
    if (!dirty.isEmpty())
        emit frameUpdated(dirty);
    return true;
}

// ExtendedDesktopSize: the framebuffer size and the screens in it (one per remote display).
bool VncSession::readExtendedDesktopSize(int w, int h)
{
    uchar head[4];
    if (!readExact(head, 4))
        return false;
    const int count = head[0];
    QList<QRect> screens;
    for (int i = 0; i < count; ++i) {
        uchar s[16];
        if (!readExact(s, 16))
            return false;
        const QRect r(qFromBigEndian<quint16>(s + 4), qFromBigEndian<quint16>(s + 6),
                      qFromBigEndian<quint16>(s + 8), qFromBigEndian<quint16>(s + 10));
        if (!r.isEmpty())
            screens.append(r);
    }
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide)
        return false;
    // Only screens inside the framebuffer; anything else is ignored rather than trusted.
    const QRect fb(0, 0, w, h);
    QList<QRect> valid;
    for (const QRect &r : std::as_const(screens))
        if (fb.contains(r) && valid.size() < 16)
            valid.append(r);
    m_screensReported = true;
    QStringList desc;
    for (const QRect &r : std::as_const(valid))
        desc << QStringLiteral("%1x%2+%3+%4").arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y());
    qInfo("vnc: server screens %dx%d: %s", w, h, qPrintable(desc.join(QStringLiteral(", "))));
    if (desktopSize() != QSize(w, h))
        resizeFrame(w, h);
    {
        QMutexLocker lock(&m_frameLock);
        m_screens = valid;
    }
    QMetaObject::invokeMethod(this, [this] { emit desktopSizeChanged(); }, Qt::QueuedConnection);
    return true;
}

bool VncSession::readRaw(int x, int y, int w, int h)
{
    QByteArray buf(w * 4, '\0');
    for (int row = 0; row < h; ++row) {
        if (!readExact(buf.data(), size_t(buf.size())))
            return false;
        QMutexLocker lock(&m_frameLock);
        auto *dst = reinterpret_cast<quint32 *>(m_frame.scanLine(y + row)) + x;
        const auto *src = reinterpret_cast<const quint32 *>(buf.constData());
        for (int i = 0; i < w; ++i)
            dst[i] = qFromLittleEndian(src[i]) | 0xff000000u;
    }
    return true;
}

bool VncSession::readCopyRect(int x, int y, int w, int h)
{
    uchar s[4];
    if (!readExact(s, 4))
        return false;
    const int sx = qFromBigEndian<quint16>(s), sy = qFromBigEndian<quint16>(s + 2);
    QMutexLocker lock(&m_frameLock);
    if (sx + w > m_frame.width() || sy + h > m_frame.height())
        return false;
    const QImage src = m_frame.copy(sx, sy, w, h);
    for (int row = 0; row < h; ++row)
        memcpy(reinterpret_cast<quint32 *>(m_frame.scanLine(y + row)) + x, src.constScanLine(row), size_t(w) * 4);
    return true;
}

bool VncSession::readZrle(int x, int y, int w, int h)
{
    quint32 len = 0;
    if (!readExact(&len, 4))
        return false;
    len = qFromBigEndian(len);
    if (len > 64 * 1024 * 1024)
        return false;
    QByteArray in(int(len), '\0');
    if (!readExact(in.data(), len))
        return false;
    QElapsedTimer cpu;   // from here on it's work on this machine, not waiting for the network
    cpu.start();
    if (!m_zrle) {
        m_zrle = new z_stream{};
        if (inflateInit(m_zrle) != Z_OK)
            return false;
    }
    // Inflate everything this rectangle carries straight into a buffer kept between rectangles (the
    // stream continues across them). At most 3 bytes a pixel and a byte or so per tile.
    const qsizetype cap = qsizetype(w) * h * 3 + (qsizetype(w) / 64 + 1) * (qsizetype(h) / 64 + 1) * 64 + 4096;
    if (m_zbuf.size() < cap)
        m_zbuf.resize(cap);
    m_zrle->next_in = reinterpret_cast<Bytef *>(in.data());
    m_zrle->avail_in = uInt(in.size());
    m_zrle->next_out = reinterpret_cast<Bytef *>(m_zbuf.data());
    m_zrle->avail_out = uInt(cap);
    const int r = inflate(m_zrle, Z_SYNC_FLUSH);
    if ((r != Z_OK && r != Z_BUF_ERROR) || m_zrle->avail_in != 0)
        return false;   // corrupt, or more than a rectangle this size can hold
    const qsizetype produced = cap - qsizetype(m_zrle->avail_out);

    const auto *p = reinterpret_cast<const uchar *>(m_zbuf.constData());
    const auto *end = p + produced;
    auto pix = [](const uchar *c) { return 0xff000000u | (quint32(c[2]) << 16) | (quint32(c[1]) << 8) | c[0]; };
    auto cpixel = [&](quint32 &out) {
        if (end - p < 3)
            return false;
        out = pix(p);
        p += 3;
        return true;
    };
    auto runLength = [&](int &run) {   // 1 + the sum of bytes up to and including the first below 255
        run = 1;
        for (;;) {
            if (p >= end)
                return false;
            const quint8 b = *p++;
            run += b;
            if (b != 255)
                return true;
        }
    };
    QMutexLocker lock(&m_frameLock);
    const qsizetype stride = m_frame.bytesPerLine() / 4;
    auto *base = reinterpret_cast<quint32 *>(m_frame.bits());
    for (int ty = 0; ty < h; ty += 64) {
        const int th = qMin(64, h - ty);
        for (int tx = 0; tx < w; tx += 64) {
            const int tw = qMin(64, w - tx);
            quint32 *tile = base + qsizetype(y + ty) * stride + (x + tx);
            if (p >= end)
                return false;
            const quint8 sub = *p++;
            const bool rle = sub & 0x80;
            const int palSize = sub & 0x7f;
            quint32 pal[128];
            if (!rle && palSize == 0) {            // raw: a row at a time
                if (end - p < qsizetype(tw) * th * 3)
                    return false;
                for (int row = 0; row < th; ++row) {
                    quint32 *dst = tile + row * stride;
                    for (int col = 0; col < tw; ++col, p += 3)
                        dst[col] = pix(p);
                }
            } else if (!rle && palSize == 1) {     // solid
                quint32 c;
                if (!cpixel(c))
                    return false;
                for (int row = 0; row < th; ++row)
                    std::fill_n(tile + row * stride, tw, c);
            } else if (!rle) {                     // packed palette
                if (palSize > 16)
                    return false;
                for (int i = 0; i < palSize; ++i)
                    if (!cpixel(pal[i]))
                        return false;
                const int bits = palSize <= 2 ? 1 : palSize <= 4 ? 2 : 4;
                const int mask = (1 << bits) - 1;
                for (int row = 0; row < th; ++row) {
                    quint32 *dst = tile + row * stride;
                    int avail = 0;   // unread bits in `byte`; every row starts on a new byte
                    quint8 byte = 0;
                    for (int col = 0; col < tw; ++col) {
                        if (avail == 0) {
                            if (p >= end)
                                return false;
                            byte = *p++;
                            avail = 8;
                        }
                        avail -= bits;
                        const int idx = (byte >> avail) & mask;
                        if (idx >= palSize)
                            return false;
                        dst[col] = pal[idx];
                    }
                }
            } else if (palSize == 0 || palSize >= 2) {   // plain RLE, or palette RLE
                if (palSize > 0)
                    for (int k = 0; k < palSize; ++k)
                        if (!cpixel(pal[k]))
                            return false;
                int row = 0, col = 0;
                const int total = tw * th;
                int done = 0;
                while (done < total) {
                    quint32 c;
                    int run = 1;
                    if (palSize == 0) {
                        if (!cpixel(c) || !runLength(run))
                            return false;
                    } else {
                        if (p >= end)
                            return false;
                        const quint8 idx = *p++;
                        if ((idx & 0x80) && !runLength(run))
                            return false;
                        if ((idx & 0x7f) >= palSize)
                            return false;
                        c = pal[idx & 0x7f];
                    }
                    if (done + run > total)
                        return false;
                    done += run;
                    while (run > 0) {   // fill along rows
                        const int n = qMin(run, tw - col);
                        std::fill_n(tile + row * stride + col, n, c);
                        run -= n;
                        col += n;
                        if (col == tw) {
                            col = 0;
                            ++row;
                        }
                    }
                }
            } else {
                return false;                      // 129 is unused
            }
        }
    }
    m_statCpuNs += cpu.nsecsElapsed();
    return true;
}

// One line every 5 s while pictures arrive: what came in and what it cost here. "decode" includes
// waiting for the rest of a rectangle's bytes, so a slow link shows up there too.
void VncSession::logStats()
{
    if (!m_statClock.isValid()) {
        m_statClock.start();
        return;
    }
    const qint64 ms = m_statClock.elapsed();
    if (ms < 5000)
        return;
    const double s = ms / 1000.0;
    const int copies = m_statCopies.exchange(0);
    const qint64 copyNs = m_statCopyNs.exchange(0);
    // "busy" is the share of the time spent decoding on this machine; the rest of "decode" is waiting
    // for the network.
    qInfo("vnc: %.0f s: %d updates, %.1f MB in (%.1f Mbit/s), %.1f Mpx, decode %.1f ms/update (cpu %.1f ms, busy %.0f%%), %d copies %.1f ms each",
          s, m_statUpdates, m_statBytes / 1e6, m_statBytes * 8 / 1e6 / s, m_statPixels / 1e6,
          m_statUpdates ? m_statDecodeNs / 1e6 / m_statUpdates : 0.0, m_statUpdates ? m_statCpuNs / 1e6 / m_statUpdates : 0.0,
          m_statCpuNs / 1e7 / s, copies, copies ? copyNs / 1e6 / copies : 0.0);
    m_statBytes = m_statPixels = 0;
    m_statUpdates = 0;
    m_statDecodeNs = 0;
    m_statCpuNs = 0;
    m_statClock.restart();
}

// --- Input ----------------------------------------------------------------------

void VncSession::sendPointer()
{
    if (m_state != QLatin1String("connected"))
        return;
    // Apple's revision reads the mask as macOS button numbers: right and middle swap places.
    quint8 mask = m_buttons;
    if (m_apple)
        mask = quint8((mask & ~0x06) | ((mask & 0x02) << 1) | ((mask & 0x04) >> 1));
    QByteArray m;
    m.append(char(5)).append(char(mask));
    putU16(m, quint16(qMax(0, m_px)));
    putU16(m, quint16(qMax(0, m_py)));
    writeAll(m.constData(), size_t(m.size()));
}

void VncSession::sendMouseMove(int x, int y)
{
    m_px = x;
    m_py = y;
    sendPointer();
}

void VncSession::sendMouseButton(Qt::MouseButton button, bool down, int x, int y)
{
    const quint8 bit = button == Qt::LeftButton ? 1 : button == Qt::MiddleButton ? 2 : button == Qt::RightButton ? 4 : 0;
    if (!bit)
        return;
    m_px = x;
    m_py = y;
    m_buttons = down ? (m_buttons | bit) : (m_buttons & ~bit);
    sendPointer();
}

void VncSession::sendWheel(int delta, bool horizontal, int x, int y)
{
    // One click of buttons 4/5 (vertical) or 6/7 (horizontal) per 120 units, at least one.
    m_px = x;
    m_py = y;
    const quint8 bit = horizontal ? (delta > 0 ? 64 : 32) : (delta > 0 ? 8 : 16);
    for (int n = qMax(1, qAbs(delta) / 120); n > 0; --n) {
        m_buttons |= bit;
        sendPointer();
        m_buttons &= ~bit;
        sendPointer();
    }
}

void VncSession::sendKeysym(quint32 keysym, bool down)
{
    if (!keysym || m_state != QLatin1String("connected"))
        return;
    if (down)
        m_heldKeys.insert(keysym);
    else if (!m_heldKeys.remove(keysym))
        return;
    // A Mac maps Alt to Command and Meta to Option, so Option goes out as Meta there.
    quint32 sent = keysym;
    if (m_apple && (keysym == 0xffe9 || keysym == 0xffea))
        sent = keysym - 2;
    QByteArray m;
    m.append(char(4)).append(char(down ? 1 : 0)).append(2, '\0');
    putU32(m, sent);
    writeAll(m.constData(), size_t(m.size()));
}

void VncSession::sendKey(unsigned scancode, bool down)
{
    sendKeysym(keysymForScancode(scancode), down);
}

void VncSession::releaseKeys()
{
    const QSet<quint32> held = m_heldKeys;
    for (quint32 k : held)
        sendKeysym(k, false);
}

quint32 VncSession::keysymForScancode(unsigned sc)
{
    // US layout, for callers that only have scancodes (the view sends keysyms to VNC sessions).
    static const QHash<unsigned, quint32> table = {
        {0x01, 0xff1b}, {0x0e, 0xff08}, {0x0f, 0xff09}, {0x1c, 0xff0d}, {0x39, 0x20},
        {0x1d, 0xffe3}, {0x11d, 0xffe4}, {0x2a, 0xffe1}, {0x36, 0xffe2}, {0x38, 0xffe9}, {0x138, 0xffea},
        {0x15b, 0xffeb}, {0x15c, 0xffec}, {0x3a, 0xffe5},
        {0x148, 0xff52}, {0x150, 0xff54}, {0x14b, 0xff51}, {0x14d, 0xff53},
        {0x147, 0xff50}, {0x14f, 0xff57}, {0x149, 0xff55}, {0x151, 0xff56}, {0x152, 0xff63}, {0x153, 0xffff},
        {0x02, '1'}, {0x03, '2'}, {0x04, '3'}, {0x05, '4'}, {0x06, '5'}, {0x07, '6'}, {0x08, '7'},
        {0x09, '8'}, {0x0a, '9'}, {0x0b, '0'}, {0x0c, '-'}, {0x0d, '='},
        {0x10, 'q'}, {0x11, 'w'}, {0x12, 'e'}, {0x13, 'r'}, {0x14, 't'}, {0x15, 'y'}, {0x16, 'u'},
        {0x17, 'i'}, {0x18, 'o'}, {0x19, 'p'}, {0x1a, '['}, {0x1b, ']'},
        {0x1e, 'a'}, {0x1f, 's'}, {0x20, 'd'}, {0x21, 'f'}, {0x22, 'g'}, {0x23, 'h'}, {0x24, 'j'},
        {0x25, 'k'}, {0x26, 'l'}, {0x27, ';'}, {0x28, '\''}, {0x29, '`'}, {0x2b, '\\'},
        {0x2c, 'z'}, {0x2d, 'x'}, {0x2e, 'c'}, {0x2f, 'v'}, {0x30, 'b'}, {0x31, 'n'}, {0x32, 'm'},
        {0x33, ','}, {0x34, '.'}, {0x35, '/'},
    };
    if (sc >= 0x3b && sc <= 0x44)
        return 0xffbe + (sc - 0x3b);   // F1–F10
    if (sc == 0x57 || sc == 0x58)
        return 0xffc8 + (sc - 0x57);   // F11, F12
    return table.value(sc, 0);
}

void VncSession::onLocalClipboard()
{
    // Apple's revision has its own pasteboard protocol (not implemented yet).
    if (m_state != QLatin1String("connected") || m_apple)
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty() || text == m_lastClipboard)
        return;
    m_lastClipboard = text;
    const QByteArray latin = text.toLatin1();   // RFB cut text is Latin-1
    QByteArray m;
    m.append(char(6)).append(3, '\0');
    putU32(m, quint32(latin.size()));
    m.append(latin);
    writeAll(m.constData(), size_t(m.size()));
}
