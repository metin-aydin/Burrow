#include "rdp/AgentLink.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <chrono>

#include "net/LocalSocket.h"

#include <libnetcore.h>

namespace {
constexpr int kTimeoutMs = 8000;
constexpr auto kBeatEvery = std::chrono::seconds(5);

bool sameMac(const QString &a, const QString &b)   // constant time
{
    if (a.size() != b.size())
        return false;
    ushort diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= a.at(i).unicode() ^ b.at(i).unicode();
    return diff == 0;
}

// One line from fd (at most 4 KB), or empty on timeout, close or overflow. `buffered` keeps what follows.
QByteArray readLine(int fd, QByteArray &buffered)
{
    while (!buffered.contains('\n') && buffered.size() < 4096) {
        if (localsocket::waitReadable(fd, kTimeoutMs) <= 0)
            return {};
        char buf[512];
        const long r = localsocket::receive(fd, buf, sizeof buf);
        if (r <= 0)
            return {};
        buffered.append(buf, int(r));
    }
    const qsizetype nl = buffered.indexOf('\n');
    if (nl < 0)
        return {};
    const QByteArray line = buffered.left(nl);
    buffered.remove(0, nl + 1);
    return line;
}

QString hmac(const QByteArray &key, const QStringList &parts)
{
    return QString::fromLatin1(
        QMessageAuthenticationCode::hash(parts.join(QLatin1Char('\n')).toUtf8(), key, QCryptographicHash::Sha256).toHex());
}
} // namespace

AgentLink::AgentLink(int tunnelHandle, const QString &host, quint16 port, const QByteArray &key)
    : m_tunnel(tunnelHandle), m_host(host), m_port(port), m_key(key)
{
}

AgentLink::~AgentLink()
{
    stop();
}

QByteArray AgentLink::keyFromCode(const QString &code)
{
    static const QByteArray alphabet("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    for (const QChar c : code.toUpper()) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('-') || c.isSpace())
            continue;
        const int v = alphabet.indexOf(char(c.toLatin1()));
        if (v < 0 || c.unicode() > 127)
            return {};
        buffer = (buffer << 5) | quint32(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((buffer >> bits) & 0xff));
        }
    }
    return out.size() == 32 ? out : QByteArray();
}

QString AgentLink::requestMac(const QByteArray &key, const QString &cmd, const QString &challenge)
{
    return hmac(key, {QStringLiteral("burrow-agent/2 request"), cmd, challenge});
}

QString AgentLink::replyMac(const QByteArray &key, const QString &cmd, const QString &challenge, bool ok)
{
    return hmac(key, {QStringLiteral("burrow-agent/2 reply"), cmd, challenge, ok ? QStringLiteral("true") : QStringLiteral("false")});
}

QString AgentLink::send(const char *cmd)
{
    int fds[2];
    if (!localsocket::pair(fds))
        return QStringLiteral("no socket");
    QByteArray target = QStringLiteral("%1:%2").arg(m_host).arg(m_port).toUtf8();
    if (NetcoreConnectFD(m_tunnel, target.data(), fds[1]) != 0) {
        localsocket::close(fds[0]);
        return QStringLiteral("can't open a connection to %1").arg(QString::fromUtf8(target));
    }
    const int fd = fds[0];
    QByteArray buffered;
    const QJsonObject hello = QJsonDocument::fromJson(readLine(fd, buffered)).object();
    const QString challenge = hello.value("challenge").toString();
    if (challenge.size() != 32) {
        localsocket::close(fd);
        return QStringLiteral("no burrow-agent at %1 (not running, or a wrong port)").arg(QString::fromUtf8(target));
    }
    const QString c = QString::fromLatin1(cmd);
    QJsonObject req{{"v", 2}, {"cmd", c}, {"mac", requestMac(m_key, c, challenge)}};
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    for (qsizetype off = 0; off < line.size();) {
        const long w = localsocket::sendSome(fd, line.constData() + off, size_t(line.size() - off));
        if (w <= 0) {
            localsocket::close(fd);
            return QStringLiteral("connection failed");
        }
        off += w;
    }
    const QByteArray reply = readLine(fd, buffered);
    localsocket::close(fd);
    if (reply.isEmpty())
        return QStringLiteral("no reply (wrong pairing code?)");
    const QJsonObject r = QJsonDocument::fromJson(reply).object();
    const bool ok = r.value("ok").toBool();
    if (!sameMac(r.value("mac").toString(), replyMac(m_key, c, challenge, ok)))
        return QStringLiteral("the reply isn't signed with this pairing code");
    if (!ok)
        return r.value("error").toString(QStringLiteral("refused"));
    if (c == QLatin1String("start"))
        m_started = true;
    return {};
}

void AgentLink::startBeats()
{
    if (m_beats.joinable())
        return;
    m_beats = std::thread([this] {
        std::unique_lock<std::mutex> lock(m_mu);
        while (!m_cv.wait_for(lock, kBeatEvery, [this] { return m_stopping; })) {
            lock.unlock();
            const QString err = send("beat");
            if (!err.isEmpty())
                qWarning("agent: beat failed: %s", qPrintable(err));
            lock.lock();
        }
    });
}

void AgentLink::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mu);
        if (m_stopping)
            return;
        m_stopping = true;
    }
    m_cv.notify_all();
    if (m_beats.joinable())
        m_beats.join();
    // Lock the office screen again right away (the agent would after its lease runs out anyway).
    if (m_started) {
        const QString err = send("end");
        if (!err.isEmpty())
            qWarning("agent: end failed: %s (the agent locks the screen when its lease runs out)", qPrintable(err));
    }
}
