#include "vnc/VncSession.h"

#include <QClipboard>
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

QImage VncSession::frame() const
{
    QMutexLocker lock(&m_frameLock);
    return m_frame.copy();
}

QImage VncSession::frame(const QRect &part) const
{
    QMutexLocker lock(&m_frameLock);
    return m_frame.copy(part.intersected(m_frame.rect()));
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
        p += got;
        n -= size_t(got);
    }
    return true;
}

bool VncSession::writeAll(const void *buf, size_t n)
{
    QMutexLocker lock(&m_writeLock);
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
        setState(QStringLiteral("closed"), ok || userClosed ? QString() : tr("The connection to %1 was lost.").arg(m_p.title));
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
    // Version 3.8 (Apple's 3.889 also accepts it).
    if (!writeAll("RFB 003.008\n", 12))
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
    else if (types.contains(char(SecNone)))
        chosen = SecNone;
    else if (types.contains(char(SecApple)))
        return tr("This Mac signs in with a user name. Enter the Mac's user name and password in the device settings.");
    else
        return tr("This VNC server asks for a sign-in method Burrow doesn't support yet.");
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
        // Never retried automatically: a wrong password must not lock the account.
        quint32 len = 0;
        if (readExact(&len, 4)) {
            len = qMin<quint32>(qFromBigEndian(len), 4096);
            QByteArray reason(int(len), '\0');
            readExact(reason.data(), len);
        }
        return tr("The user name or password for %1 is wrong.").arg(m_p.title);
    }

    setPhase(QStringLiteral("opening"));
    const quint8 shared = 1;   // don't disconnect other viewers
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

    // 32-bit true colour, little endian, 0x00RRGGBB: the same bytes as QImage::Format_RGB32.
    QByteArray pf;
    pf.append(char(0)).append(3, '\0');                       // SetPixelFormat + padding
    pf.append(char(32)).append(char(24)).append(char(0)).append(char(1));
    putU16(pf, 255); putU16(pf, 255); putU16(pf, 255);
    pf.append(char(16)).append(char(8)).append(char(0)).append(3, '\0');
    QByteArray enc;
    enc.append(char(2)).append(char(0));
    const qint32 encodings[] = {EncZrle, EncCopyRect, EncRaw, EncExtendedDesktopSize, EncDesktopSize};
    putU16(enc, quint16(std::size(encodings)));
    for (qint32 e : encodings)
        putU32(enc, quint32(e));
    if (!writeAll(pf.constData(), size_t(pf.size())) || !writeAll(enc.constData(), size_t(enc.size())))
        return unreachable;
    requestUpdate(false);
    return {};
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
    bool ok = ctx && p && g && y && priv && pub && secret
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
            requestUpdate(true);
            break;
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
        dirty = dirty.united(QRect(x, y, w, hh));
    }
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
    if (!m_zrle) {
        m_zrle = new z_stream{};
        if (inflateInit(m_zrle) != Z_OK)
            return false;
    }
    // Inflate everything this rectangle carries (the stream continues across rectangles).
    m_zbuf.resize(0);
    m_zrle->next_in = reinterpret_cast<Bytef *>(in.data());
    m_zrle->avail_in = uInt(in.size());
    const qsizetype cap = qsizetype(w) * h * 4 + 4096 + (w / 64 + 1) * (h / 64 + 1) * 64;
    char chunk[65536];
    while (m_zrle->avail_in > 0) {
        m_zrle->next_out = reinterpret_cast<Bytef *>(chunk);
        m_zrle->avail_out = sizeof chunk;
        const int r = inflate(m_zrle, Z_SYNC_FLUSH);
        if (r != Z_OK && r != Z_BUF_ERROR)
            return false;
        m_zbuf.append(chunk, qsizetype(sizeof chunk - m_zrle->avail_out));
        if (m_zbuf.size() > cap)
            return false;
        if (r == Z_BUF_ERROR && m_zrle->avail_out != 0)
            break;
    }

    const auto *p = reinterpret_cast<const uchar *>(m_zbuf.constData());
    const auto *end = p + m_zbuf.size();
    auto cpixel = [&](quint32 &out) {
        if (end - p < 3)
            return false;
        out = 0xff000000u | (quint32(p[2]) << 16) | (quint32(p[1]) << 8) | p[0];
        p += 3;
        return true;
    };
    QMutexLocker lock(&m_frameLock);
    for (int ty = 0; ty < h; ty += 64) {
        const int th = qMin(64, h - ty);
        for (int tx = 0; tx < w; tx += 64) {
            const int tw = qMin(64, w - tx);
            if (p >= end)
                return false;
            const quint8 sub = *p++;
            const bool rle = sub & 0x80;
            const int palSize = sub & 0x7f;
            quint32 pal[128];
            auto put = [&](int i, quint32 c) {
                reinterpret_cast<quint32 *>(m_frame.scanLine(y + ty + i / tw))[x + tx + i % tw] = c;
            };
            if (!rle && palSize == 0) {            // raw
                for (int i = 0; i < tw * th; ++i) {
                    quint32 c;
                    if (!cpixel(c))
                        return false;
                    put(i, c);
                }
            } else if (!rle && palSize == 1) {     // solid
                quint32 c;
                if (!cpixel(c))
                    return false;
                for (int i = 0; i < tw * th; ++i)
                    put(i, c);
            } else if (!rle) {                     // packed palette
                if (palSize > 16)
                    return false;
                for (int i = 0; i < palSize; ++i)
                    if (!cpixel(pal[i]))
                        return false;
                const int bits = palSize <= 2 ? 1 : palSize <= 4 ? 2 : 4;
                const int mask = (1 << bits) - 1;
                for (int row = 0; row < th; ++row) {
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
                        put(row * tw + col, pal[idx]);
                    }
                }
            } else if (palSize == 0) {             // plain RLE
                int i = 0;
                while (i < tw * th) {
                    quint32 c;
                    if (!cpixel(c))
                        return false;
                    int run = 1;
                    for (;;) {
                        if (p >= end)
                            return false;
                        const quint8 b = *p++;
                        run += b;
                        if (b != 255)
                            break;
                    }
                    if (i + run > tw * th)
                        return false;
                    for (int k = 0; k < run; ++k)
                        put(i++, c);
                }
            } else if (palSize >= 2) {             // palette RLE
                for (int k = 0; k < palSize; ++k)
                    if (!cpixel(pal[k]))
                        return false;
                int i = 0;
                while (i < tw * th) {
                    if (p >= end)
                        return false;
                    quint8 idx = *p++;
                    int run = 1;
                    if (idx & 0x80) {
                        idx &= 0x7f;
                        for (;;) {
                            if (p >= end)
                                return false;
                            const quint8 b = *p++;
                            run += b;
                            if (b != 255)
                                break;
                        }
                    }
                    if (idx >= palSize || i + run > tw * th)
                        return false;
                    for (int k = 0; k < run; ++k)
                        put(i++, pal[idx]);
                }
            } else {
                return false;                      // 129 is unused
            }
        }
    }
    return true;
}

// --- Input ----------------------------------------------------------------------

void VncSession::sendPointer()
{
    if (m_state != QLatin1String("connected"))
        return;
    QByteArray m;
    m.append(char(5)).append(char(m_buttons));
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
    QByteArray m;
    m.append(char(4)).append(char(down ? 1 : 0)).append(2, '\0');
    putU32(m, keysym);
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
    if (m_state != QLatin1String("connected"))
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
