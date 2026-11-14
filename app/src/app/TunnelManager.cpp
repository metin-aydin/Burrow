#include "app/TunnelManager.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <libnetcore.h>

#include "vault/VaultStore.h"

namespace {
QVariantMap parseStatus(char *raw)
{
    QVariantMap m = QJsonDocument::fromJson(QByteArray(raw ? raw : "{}")).object().toVariantMap();
    if (raw)
        NetcoreFree(raw);
    return m;
}

void wipe(QByteArray &b)
{
    std::fill(b.begin(), b.end(), '\0');
    b.clear();
}
} // namespace

TunnelManager::TunnelManager(VaultStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &TunnelManager::poll);
}

TunnelManager::~TunnelManager()
{
    stopAll();
}

bool TunnelManager::needsPassword(const Model::Tunnel &t) const
{
    return t.type == Model::Tunnel::Type::GlobalProtect
        && (!t.gpSavePassword || t.gpPassword.isEmpty()) && !m_passwords.contains(t.id);
}

QString TunnelManager::ensure(const Model::Tunnel &t, const QString &password, const QString &target)
{
    if (m_handles.contains(t.id)) {
        if (!target.isEmpty() && t.type == Model::Tunnel::Type::Cloudflare) {
            QByteArray tg = target.toUtf8();
            NetcoreTunnelPrepare(m_handles.value(t.id), tg.data());
            m_states.insert(t.id, QVariantMap{{"state", "starting"}});
            emit statesChanged();
            if (!m_timer.isActive())
                m_timer.start();
        }
        return {};
    }
    int handle = 0;
    if (t.type == Model::Tunnel::Type::Tailscale) {
        const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                .filePath(QStringLiteral("tunnels/") + t.id);
        QByteArray id = t.id.toUtf8(), host = t.tsHostname.toUtf8(), control = t.tsControlUrl.toUtf8(),
                   d = dir.toUtf8();
        handle = NetcoreTunnelStartTailscale(m_store->handle(), id.data(), host.data(), control.data(), d.data());
        if (handle <= 0)
            return tr("Couldn't start Tailscale.");
    } else if (t.type == Model::Tunnel::Type::Cloudflare) {
        QByteArray id = t.id.toUtf8(), bastion = t.cfBastion.toUtf8();
        handle = NetcoreTunnelStartCloudflare(m_store->handle(), id.data(), bastion.data());
        if (handle <= 0)
            return tr("Couldn't start the Cloudflare tunnel.");
        if (!target.isEmpty()) {
            QByteArray tg = target.toUtf8();
            NetcoreTunnelPrepare(handle, tg.data());
        }
    } else {
        if (!password.isEmpty())
            m_passwords.insert(t.id, password);
        const QString pw = t.gpSavePassword && !t.gpPassword.isEmpty() ? t.gpPassword : m_passwords.value(t.id);
        if (pw.isEmpty())
            return tr("Enter the VPN password.");
        QJsonObject p = Model::toJson(t).value("gp").toObject();
        p["password"] = pw;
        QByteArray json = QJsonDocument(p).toJson(QJsonDocument::Compact);
        char *err = nullptr;
        handle = NetcoreTunnelStartGlobalProtect(json.data(), &err);
        wipe(json);
        if (handle <= 0) {
            const QVariantMap st = parseStatus(err);
            return st.value("error").toString();
        }
    }
    m_handles.insert(t.id, handle);
    m_states.insert(t.id, QVariantMap{{"state", "starting"}});
    emit statesChanged();
    if (!m_timer.isActive())
        m_timer.start();
    return {};
}

QVariantMap TunnelManager::status(const QString &tunnelId) const
{
    return m_states.value(tunnelId).toMap();
}

bool TunnelManager::isConnected(const QString &tunnelId) const
{
    return status(tunnelId).value("state").toString() == QLatin1String("connected");
}

QString TunnelManager::openForward(const QString &tunnelId, const QString &target, int *forwardHandle)
{
    const int h = m_handles.value(tunnelId);
    if (h <= 0)
        return {};
    QByteArray t = target.toUtf8();
    char *addr = nullptr;
    const int fh = NetcoreStartTunnelForward(h, t.data(), &addr);
    if (fh <= 0)
        return {};
    *forwardHandle = fh;
    const QString out = QString::fromUtf8(addr);
    NetcoreFree(addr);
    return out;
}

void TunnelManager::closeForward(int forwardHandle)
{
    if (forwardHandle > 0)
        NetcoreStopForward(forwardHandle);
}

void TunnelManager::sealForward(int forwardHandle)
{
    if (forwardHandle > 0)
        NetcoreForwardSeal(forwardHandle);
}

void TunnelManager::submitOtp(const QString &tunnelId, const QString &code)
{
    QByteArray c = code.toUtf8();
    NetcoreTunnelSubmitOTP(m_handles.value(tunnelId), c.data());
    wipe(c);
}

void TunnelManager::stop(const QString &tunnelId)
{
    const int h = m_handles.take(tunnelId);
    if (h > 0)
        NetcoreTunnelStop(h);
    m_states.remove(tunnelId);
    emit statesChanged();
}

void TunnelManager::stopAll()
{
    for (int h : std::as_const(m_handles))
        NetcoreTunnelStop(h);
    m_handles.clear();
    for (QString &p : m_passwords)
        p.fill(QChar(0));
    m_passwords.clear();
    m_states.clear();
    m_timer.stop();
    emit statesChanged();
}

void TunnelManager::poll()
{
    bool changed = false;
    const auto ids = m_handles.keys();
    for (const QString &id : ids) {
        const QVariantMap now = parseStatus(NetcoreTunnelStatus(m_handles.value(id)));
        const QVariantMap before = m_states.value(id).toMap();
        if (now == before)
            continue;
        changed = true;
        m_states.insert(id, now);
        const QString state = now.value("state").toString();
        if (state != before.value("state").toString()) {
            emit stateChanged(id, state, now.value("error").toString());
            if (state == QLatin1String("otp") || state == QLatin1String("needs_otp"))
                emit otpRequired(id, now.value("prompt").toString());
        }
        if (state == QLatin1String("needs_login") && now.value("authUrl") != before.value("authUrl")
            && !now.value("authUrl").toString().isEmpty())
            emit loginRequired(id, now.value("authUrl").toString());
        if (state == QLatin1String("failed") || state == QLatin1String("stopped")) {
            NetcoreTunnelStop(m_handles.take(id));
            m_passwords.remove(id);   // a rejected password isn't reused
        }
    }
    if (m_handles.isEmpty())
        m_timer.stop();
    if (changed)
        emit statesChanged();
}
