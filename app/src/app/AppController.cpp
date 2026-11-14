#include "app/AppController.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
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

// --- Vault -------------------------------------------------------------------

QString AppController::createVault(const QString &password, const QString &confirm)
{
    if (password.size() < kMinPasswordLength)
        return tr("Şifre en az %1 karakter olmalı.").arg(kMinPasswordLength);
    if (password != confirm)
        return tr("Şifreler aynı değil.");
    return finishUnlock(m_store.create(password));
}

QString AppController::unlock(const QString &password)
{
    if (password.isEmpty())
        return tr("Ana şifreni gir.");
    return finishUnlock(m_store.open(password));
}

QString AppController::unlockWithTouchId()
{
    QString err;
    QByteArray key = Biometric::loadKey(tr("Burrow kasasının kilidini aç"), &err);
    if (key.isEmpty())
        return err;
    const VaultStore::Result r = m_store.openWithKey(key);
    wipe(key);
    if (r != VaultStore::Result::Ok) {
        // A stale key (e.g. the vault was re-created) is useless.
        Biometric::removeKey();
        QSettings().setValue(kTouchIdKey, false);
        emit touchIdEnabledChanged();
        return tr("Touch ID anahtarı geçersiz; ana şifreyle aç.");
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

void AppController::lock()
{
    m_store.lock();
    m_devices.setPreviewRevisions({});
    reloadModels();
    emit stateChanged();
}

QString AppController::changePassword(const QString &current, const QString &next,
                                      const QString &again)
{
    if (next.size() < kMinPasswordLength)
        return tr("Yeni şifre en az %1 karakter olmalı.").arg(kMinPasswordLength);
    if (next != again)
        return tr("Yeni şifreler aynı değil.");
    if (!m_store.verifyPassword(current))
        return tr("Mevcut şifre yanlış.");
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
            emit toast(tr("Ana şifre değiştirildi. Touch ID kapatıldı: %1").arg(err));
            return {};
        }
    }
    emit toast(tr("Ana şifre değiştirildi."));
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
        ? QString() : tr("Değişiklik kaydedilemedi: %1").arg(VaultStore::message(r));
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
        return tr("Ad ve adres boş olamaz.");
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
        copy.name = tr("%1 (Kopya)").arg(copy.name);
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

void AppController::connectDevice(const QString &id)
{
    Q_UNUSED(id)
    emit toast(tr("RDP motoru bir sonraki adımda eklenecek."));
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
        return tr("Profil adı boş olamaz.");
    if (t.type == Model::Tunnel::Type::GlobalProtect) {
        t.gpHost = t.gpHost.trimmed();
        if (t.gpHost.isEmpty() || t.gpUsername.trimmed().isEmpty())
            return tr("Sunucu ve kullanıcı adı boş olamaz.");
        if (t.otpEnabled && t.otpSource == QLatin1String("totp")
            && NetCore::totpCode(t.totp).isEmpty())
            return tr("OTP anahtarı geçersiz.");
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
    QVariantList out{QVariantMap{{"id", QString()}, {"name", tr("Doğrudan (Tünelsiz)")},
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
        return {{"error", tr("Geçersiz anahtar")}};
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
    vpn.name = tr("Şirket VPN");
    vpn.gpHost = QStringLiteral("vpn.example.com");
    vpn.gpUsername = QStringLiteral("demo");
    Model::Tunnel ts;
    ts.id = Model::newId();
    ts.name = tr("Ev Ağı");
    ts.type = Model::Tunnel::Type::Tailscale;
    m_store.doc().tunnels = {vpn, ts};

    struct Sample { const char *name, *host, *os; QString tunnel; QColor a, b; };
    const Sample samples[] = {
        {"İş Bilgisayarı", "10.0.7.155", "ubuntu", vpn.id, QColor(233, 84, 32), QColor(119, 33, 111)},
        {"Toplantı Odası", "10.0.9.20", "windows", vpn.id, QColor(0, 120, 212), QColor(80, 40, 160)},
        {"Ev Masaüstü", "ev-pc", "windows", ts.id, QColor(20, 140, 120), QColor(10, 60, 90)},
        {"Laboratuvar", "10.0.3.8", "ubuntu", vpn.id, {}, {}},
    };
    for (const Sample &s : samples) {
        Model::Device d;
        d.id = Model::newId();
        d.name = QString::fromUtf8(s.name);
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
