#include "app/AppController.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLinearGradient>
#include <QCoreApplication>
#include <QLocale>
#include <QPainter>
#include <QQmlEngine>
#include <QSettings>

#include "app/PreviewProvider.h"
#include "net/NetCore.h"
#include "platform/Biometric.h"
#include "rdp/FreeRdpEngine.h"

namespace {
constexpr int kMinPasswordLength = 8;
const char kTouchIdKey[] = "security/touchIdEnabled";

void wipe(QByteArray &b)
{
    std::fill(b.begin(), b.end(), '\0');
    b.clear();
}
} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_devices(&m_store)
    , m_tunnels(&m_store)
{
    m_gallery.setSourceModel(&m_devices);
    connect(&m_updater, &Updater::updateAvailable, this, [this](const QString &v) {
        emit toast(tr("Burrow %1 is available. Install it from Settings.").arg(v));
    });
}

QString AppController::state() const
{
    if (m_store.isOpen())
        return QStringLiteral("unlocked");
    return m_store.exists() ? QStringLiteral("locked") : QStringLiteral("setup");
}

bool AppController::touchIdAvailable() const
{
    return Biometric::isAvailable();
}

bool AppController::touchIdEnabled() const
{
    return QSettings().value(kTouchIdKey, false).toBool();
}

QString AppController::versionInfo() const
{
    return tr("Burrow %1 · netcore %2 · %3")
        .arg(QStringLiteral(BURROW_VERSION), NetCore::version(), FreeRdpEngine::engineVersion());
}

// --- Language ----------------------------------------------------------------

namespace {
const char kLanguageKey[] = "ui/language";

QString resolveLanguage(const QString &setting)
{
    if (setting == QLatin1String("en") || setting == QLatin1String("tr"))
        return setting;
    // First run / "system": follow the device language; English otherwise.
    return QLocale::system().language() == QLocale::Turkish ? QStringLiteral("tr")
                                                            : QStringLiteral("en");
}
} // namespace

QString AppController::language() const
{
    return QSettings().value(kLanguageKey, QStringLiteral("system")).toString();
}

void AppController::setLanguage(const QString &lang)
{
    if (lang == language())
        return;
    QSettings().setValue(kLanguageKey, lang);
    applyLanguage(m_engine);
    emit languageChanged();
}

void AppController::applyLanguage(QQmlEngine *engine)
{
    if (engine)
        m_engine = engine;
    QCoreApplication::removeTranslator(&m_translator);
    // BURROW_LANG overrides the setting (screenshots, testing).
    const QString env = qEnvironmentVariable("BURROW_LANG");
    const QString lang = resolveLanguage(env.isEmpty() ? language() : env);
    // English is the source language; other languages load a compiled catalog.
    if (lang != QLatin1String("en")
        && m_translator.load(QStringLiteral(":/i18n/burrow_%1.qm").arg(lang)))
        QCoreApplication::installTranslator(&m_translator);
    QLocale::setDefault(QLocale(lang == QLatin1String("tr") ? QLocale::Turkish : QLocale::English));
    if (m_engine)
        m_engine->retranslate();
}

// --- Vault -------------------------------------------------------------------

QString AppController::createVault(const QString &password, const QString &confirm)
{
    if (password.size() < kMinPasswordLength)
        return tr("The password must be at least %1 characters.").arg(kMinPasswordLength);
    if (password != confirm)
        return tr("The passwords don't match.");
    return finishUnlock(m_store.create(password));
}

QString AppController::unlock(const QString &password)
{
    if (password.isEmpty())
        return tr("Enter your master password.");
    return finishUnlock(m_store.open(password));
}

QString AppController::unlockWithTouchId()
{
    QString err;
    QByteArray key = Biometric::loadKey(tr("Unlock the Burrow vault"), &err);
    if (key.isEmpty())
        return err;
    const VaultStore::Result r = m_store.openWithKey(key);
    wipe(key);
    if (r != VaultStore::Result::Ok) {
        // A stale key (e.g. the vault was re-created) is useless.
        Biometric::removeKey();
        QSettings().setValue(kTouchIdKey, false);
        emit touchIdEnabledChanged();
        return tr("The Touch ID key is no longer valid; unlock with your master password.");
    }
    return finishUnlock(r);
}

QString AppController::finishUnlock(VaultStore::Result r)
{
    if (r != VaultStore::Result::Ok)
        return VaultStore::message(r);
    QHash<QString, int> revs;
    for (const Model::Device &d : m_store.doc().devices)
        if (QFileInfo::exists(blobPath(PreviewProvider::blobName(d.id))))
            revs.insert(d.id, 1);
    m_devices.setPreviewRevisions(revs);
    reloadModels();
    emit stateChanged();
    return {};
}

void AppController::shutdown()
{
    for (RdpSession *s : std::as_const(m_sessions)) {
        s->close();
        if (m_store.isOpen())
            setPreview(s->deviceId(), s->frame());
    }
    qDeleteAll(m_sessions);   // joins the worker threads
    m_sessions.clear();
    m_store.lock();
}

void AppController::lock()
{
    for (RdpSession *s : std::as_const(m_sessions)) {
        s->close();
        setPreview(s->deviceId(), s->frame());
    }
    m_store.lock();
    m_devices.setPreviewRevisions({});
    reloadModels();
    emit stateChanged();
}

QString AppController::changePassword(const QString &current, const QString &next,
                                      const QString &again)
{
    if (next.size() < kMinPasswordLength)
        return tr("The new password must be at least %1 characters.").arg(kMinPasswordLength);
    if (next != again)
        return tr("The new passwords don't match.");
    if (!m_store.verifyPassword(current))
        return tr("The current password is wrong.");
    const VaultStore::Result r = m_store.changePassword(next);
    if (r != VaultStore::Result::Ok)
        return VaultStore::message(r);
    // The data key was rotated (ADR 0004); refresh the Touch ID copy.
    if (touchIdEnabled()) {
        QString err;
        QByteArray key = m_store.exportKey();
        const bool ok = Biometric::storeKey(key, &err);
        wipe(key);
        if (!ok) {
            Biometric::removeKey();
            QSettings().setValue(kTouchIdKey, false);
            emit touchIdEnabledChanged();
            emit toast(tr("Master password changed. Touch ID was turned off: %1").arg(err));
            return {};
        }
    }
    emit toast(tr("Master password changed."));
    return {};
}

QString AppController::setTouchIdEnabled(bool on)
{
    if (!on) {
        Biometric::removeKey();
        QSettings().setValue(kTouchIdKey, false);
        emit touchIdEnabledChanged();
        return {};
    }
    QString err;
    QByteArray key = m_store.exportKey();
    const bool ok = Biometric::storeKey(key, &err);
    wipe(key);
    if (!ok) {
        emit touchIdEnabledChanged(); // let the switch snap back
        return err;
    }
    QSettings().setValue(kTouchIdKey, true);
    emit touchIdEnabledChanged();
    return {};
}

QString AppController::persist()
{
    const VaultStore::Result r = m_store.save();
    return r == VaultStore::Result::Ok
        ? QString() : tr("Couldn't save the change: %1").arg(VaultStore::message(r));
}

void AppController::reloadModels()
{
    m_devices.reload();
    m_tunnels.reload();
}

QString AppController::blobPath(const QString &name) const
{
    return QDir(QFileInfo(m_store.path()).absolutePath()).filePath(QStringLiteral("blobs/%1.bin").arg(name));
}

// --- Devices -----------------------------------------------------------------

QVariantMap AppController::newDevice() const
{
    return Model::toJson(Model::Device{}).toVariantMap();
}

QVariantMap AppController::device(const QString &id) const
{
    for (const Model::Device &d : m_store.doc().devices)
        if (d.id == id) {
            // toJson drops unsaved passwords, which is what the editor should see.
            return Model::toJson(d).toVariantMap();
        }
    return {};
}

QString AppController::saveDevice(const QVariantMap &map)
{
    Model::Device d = Model::deviceFromJson(QJsonObject::fromVariantMap(map));
    d.name = d.name.trimmed();
    d.host = d.host.trimmed();
    if (d.name.isEmpty() || d.host.isEmpty())
        return tr("Name and address can't be empty.");
    if (!d.savePassword) {
        d.password.clear();
        d.autoLogin = false;
    }
    auto &devices = m_store.doc().devices;
    if (d.id.isEmpty()) {
        d.id = Model::newId();
        devices.append(d);
    } else {
        bool found = false;
        for (Model::Device &existing : devices)
            if (existing.id == d.id) {
                d.layout = existing.layout; // not edited in the form
                existing = d;
                found = true;
            }
        if (!found)
            devices.append(d);
    }
    const QString err = persist();
    reloadModels();
    return err;
}

void AppController::duplicateDevice(const QString &id)
{
    auto &devices = m_store.doc().devices;
    for (int i = 0; i < devices.size(); ++i) {
        if (devices.at(i).id != id)
            continue;
        Model::Device copy = devices.at(i);
        copy.id = Model::newId();
        copy.name = tr("%1 (Copy)").arg(copy.name);
        copy.layout = {};
        devices.insert(i + 1, copy);
        break;
    }
    const QString err = persist();
    reloadModels();
    if (!err.isEmpty())
        emit toast(err);
}

void AppController::deleteDevice(const QString &id)
{
    auto &devices = m_store.doc().devices;
    for (int i = 0; i < devices.size(); ++i)
        if (devices.at(i).id == id) {
            devices.removeAt(i);
            break;
        }
    m_store.deleteBlob(PreviewProvider::blobName(id));
    m_devices.previewChanged(id, false);
    const QString err = persist();
    reloadModels();
    if (!err.isEmpty())
        emit toast(err);
}

bool AppController::needsCredentials(const QString &id) const
{
    for (const Model::Device &d : m_store.doc().devices)
        if (d.id == id)
            return d.username.isEmpty() || !d.savePassword || d.password.isEmpty();
    return false;
}

QString AppController::connectDevice(const QString &id, const QString &username, const QString &password)
{
    const Model::Device *dev = nullptr;
    for (const Model::Device &d : m_store.doc().devices)
        if (d.id == id)
            dev = &d;
    if (!dev)
        return tr("The device no longer exists.");
    for (RdpSession *s : std::as_const(m_sessions))
        if (s->deviceId() == id)
            return tr("%1 is already open.").arg(dev->name);
    if (!dev->tunnelId.isEmpty())
        return tr("Tunnels inside Burrow are coming next. For now, use a direct connection.");

    const QString user = username.isEmpty() ? dev->username : username;
    const QString pass = password.isEmpty() ? (dev->savePassword ? dev->password : QString()) : password;
    if (user.isEmpty() || pass.isEmpty())
        return QStringLiteral("credentials");

    RdpSession::Params p;
    p.deviceId = dev->id;
    p.title = dev->name;
    p.host = dev->host;
    p.port = quint16(dev->port);
    p.username = user;
    p.password = pass;
    p.domain = dev->domain;
    p.keyMap = dev->keyMap;
    p.lockOnDisconnect = dev->lockOnDisconnect;   // Win+L on Windows, Super+L on GNOME
    p.knownFingerprint = dev->certFingerprint;
    const QJsonObject layout = dev->layout;
    if (layout.contains("width"))
        p.size = QSize(layout.value("width").toInt(1600), layout.value("height").toInt(1000));

    auto *session = new RdpSession(p, this);
    m_sessions.append(session);
    connect(session, &RdpSession::certificateTrusted, this, [this](const QString &devId, const QString &fp) {
        for (Model::Device &d : m_store.doc().devices)
            if (d.id == devId)
                d.certFingerprint = fp;
        persist();
    });
    connect(session, &RdpSession::closed, this, [this](RdpSession *s) {
        const QImage last = s->frame();
        if (!last.isNull() && m_store.isOpen())
            setPreview(s->deviceId(), last);
        if (!s->error().isEmpty())
            emit toast(s->error());
        m_sessions.removeOne(s);
        s->deleteLater();
    });
    session->start();
    emit sessionOpened(session);
    return {};
}

void AppController::forgetCertificate(const QString &id)
{
    for (Model::Device &d : m_store.doc().devices)
        if (d.id == id)
            d.certFingerprint.clear();
    persist();
}

void AppController::setPreview(const QString &deviceId, const QImage &image)
{
    if (image.isNull())
        return;
    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    image.scaledToWidth(qMin(640, image.width()), Qt::SmoothTransformation).save(&buf, "JPEG", 80);
    if (m_store.putBlob(PreviewProvider::blobName(deviceId), jpeg))
        m_devices.previewChanged(deviceId, true);
    wipe(jpeg);
}

// --- Tunnels -----------------------------------------------------------------

QVariantMap AppController::newTunnel() const
{
    Model::Tunnel t;
    t.tsHostname = QStringLiteral("burrow");
    return Model::toJson(t).toVariantMap();
}

QVariantMap AppController::tunnel(const QString &id) const
{
    const Model::Tunnel *t = m_store.doc().tunnel(id);
    return t ? Model::toJson(*t).toVariantMap() : QVariantMap{};
}

QString AppController::saveTunnel(const QVariantMap &map)
{
    Model::Tunnel t = Model::tunnelFromJson(QJsonObject::fromVariantMap(map));
    t.name = t.name.trimmed();
    if (t.name.isEmpty())
        return tr("The profile name can't be empty.");
    if (t.type == Model::Tunnel::Type::GlobalProtect) {
        t.gpHost = t.gpHost.trimmed();
        if (t.gpHost.isEmpty() || t.gpUsername.trimmed().isEmpty())
            return tr("Server and username can't be empty.");
        if (t.otpEnabled && t.otpSource == QLatin1String("totp")
            && NetCore::totpCode(t.totp).isEmpty())
            return tr("The OTP key is invalid.");
        if (!t.otpEnabled || t.otpSource != QLatin1String("totp"))
            t.totp = {};
    }
    auto &tunnels = m_store.doc().tunnels;
    if (t.id.isEmpty()) {
        t.id = Model::newId();
        tunnels.append(t);
    } else {
        bool found = false;
        for (Model::Tunnel &existing : tunnels)
            if (existing.id == t.id) {
                existing = t;
                found = true;
            }
        if (!found)
            tunnels.append(t);
    }
    const QString err = persist();
    reloadModels();
    return err;
}

void AppController::deleteTunnel(const QString &id)
{
    for (Model::Device &d : m_store.doc().devices)
        if (d.tunnelId == id)
            d.tunnelId.clear();
    auto &tunnels = m_store.doc().tunnels;
    for (int i = 0; i < tunnels.size(); ++i)
        if (tunnels.at(i).id == id) {
            tunnels.removeAt(i);
            break;
        }
    const QString err = persist();
    reloadModels();
    if (!err.isEmpty())
        emit toast(err);
}

QVariantList AppController::tunnelChoices() const
{
    QVariantList out{QVariantMap{{"id", QString()}, {"name", tr("Direct (No Tunnel)")},
                                 {"type", QStringLiteral("direct")}}};
    for (const Model::Tunnel &t : m_store.doc().tunnels)
        out.append(QVariantMap{{"id", t.id}, {"name", t.name},
                               {"type", t.type == Model::Tunnel::Type::Tailscale
                                            ? QStringLiteral("tailscale")
                                            : QStringLiteral("globalprotect")}});
    return out;
}

// --- TOTP --------------------------------------------------------------------

QVariantList AppController::parseTotp(const QString &uri) const
{
    QVariantList out;
    for (const Model::Totp &t : NetCore::parseTotp(uri))
        out.append(QVariantMap{{"secret", t.secret}, {"algorithm", t.algorithm},
                               {"digits", t.digits}, {"period", t.period},
                               {"issuer", t.issuer}, {"account", t.account}});
    return out;
}

QVariantMap AppController::totpCode(const QVariantMap &m) const
{
    Model::Totp t;
    t.secret = m.value("secret").toString().remove(QLatin1Char(' ')).trimmed().toUpper();
    t.algorithm = m.value("algorithm").toString();
    t.digits = m.value("digits").toInt();
    t.period = m.value("period").toInt();
    if (t.secret.isEmpty())
        return {};
    int left = 0;
    const QString code = NetCore::totpCode(t, &left);
    if (code.isEmpty())
        return {{"error", tr("Invalid key")}};
    return {{"code", code}, {"remaining", left}};
}

// --- Demo --------------------------------------------------------------------

namespace {
// Paints a desktop-like picture so the gallery can be previewed before the
// RDP engine exists.
QImage demoDesktop(const QColor &a, const QColor &b, bool ubuntu)
{
    QImage img(1280, 800, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 1280, 800);
    g.setColorAt(0, a);
    g.setColorAt(1, b);
    p.fillRect(img.rect(), g);
    if (ubuntu) {
        p.fillRect(0, 0, 1280, 28, QColor(20, 20, 20));
        p.fillRect(0, 28, 64, 772, QColor(30, 30, 30, 220));
        for (int i = 0; i < 6; ++i) {
            p.setBrush(QColor::fromHsv((i * 50) % 360, 160, 220));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(12, 48 + i * 60, 40, 40, 10, 10);
        }
    } else {
        p.fillRect(0, 752, 1280, 48, QColor(32, 32, 40, 235));
        for (int i = 0; i < 7; ++i) {
            p.setBrush(QColor::fromHsv((200 + i * 30) % 360, 120, 230));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(470 + i * 48, 760, 32, 32, 6, 6);
        }
    }
    // A couple of "windows".
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(250, 250, 252));
    p.drawRoundedRect(180, 110, 620, 420, 12, 12);
    p.setBrush(QColor(235, 236, 240));
    p.drawRoundedRect(180, 110, 620, 36, 12, 12);
    p.setBrush(QColor(40, 42, 54));
    p.drawRoundedRect(640, 300, 520, 340, 12, 12);
    p.setBrush(QColor(90, 200, 120));
    for (int i = 0; i < 8; ++i)
        p.drawRect(670, 340 + i * 30, 120 + (i * 53) % 260, 10);
    p.setBrush(QColor(200, 205, 215));
    for (int i = 0; i < 9; ++i)
        p.drawRect(210, 170 + i * 36, 300 + (i * 71) % 240, 12);
    return img;
}
} // namespace

void AppController::seedDemo()
{
    if (!m_store.isOpen() || !m_store.doc().devices.isEmpty())
        return;
    Model::Tunnel vpn;
    vpn.id = Model::newId();
    vpn.name = tr("Company VPN");
    vpn.gpHost = QStringLiteral("vpn.example.com");
    vpn.gpUsername = QStringLiteral("demo");
    Model::Tunnel ts;
    ts.id = Model::newId();
    ts.name = tr("Home Network");
    ts.type = Model::Tunnel::Type::Tailscale;
    m_store.doc().tunnels = {vpn, ts};

    struct Sample { const char *name, *host, *os; QString tunnel; QColor a, b; };
    const Sample samples[] = {
        {QT_TR_NOOP("Work PC"), "10.0.7.155", "ubuntu", vpn.id, QColor(233, 84, 32), QColor(119, 33, 111)},
        {QT_TR_NOOP("Meeting Room"), "10.0.9.20", "windows", vpn.id, QColor(0, 120, 212), QColor(80, 40, 160)},
        {QT_TR_NOOP("Home Desktop"), "home-pc", "windows", ts.id, QColor(20, 140, 120), QColor(10, 60, 90)},
        {QT_TR_NOOP("Lab Server"), "10.0.3.8", "ubuntu", vpn.id, {}, {}},
    };
    for (const Sample &s : samples) {
        Model::Device d;
        d.id = Model::newId();
        d.name = tr(s.name);
        d.host = QString::fromUtf8(s.host);
        d.os = QString::fromUtf8(s.os);
        d.tunnelId = s.tunnel;
        m_store.doc().devices.append(d);
        if (s.a.isValid())
            setPreview(d.id, demoDesktop(s.a, s.b, d.os == QLatin1String("ubuntu")));
    }
    persist();
    reloadModels();
}
