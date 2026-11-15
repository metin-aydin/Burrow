#include "vault/VaultStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <libnetcore.h>

namespace {

VaultStore::Result fromCode(int code)
{
    switch (code) {
    case 0: return VaultStore::Result::Ok;
    case 1: return VaultStore::Result::WrongPassword;
    case 2: return VaultStore::Result::Corrupt;
    case 3: return VaultStore::Result::Exists;
    default: return VaultStore::Result::IoError;
    }
}

// Zero a QByteArray holding secret material before it is released.
void wipe(QByteArray &b)
{
    if (!b.isEmpty())
        std::fill(b.begin(), b.end(), '\0');
    b.clear();
}

} // namespace

VaultStore::VaultStore()
    : m_path(defaultPath())
{
}

VaultStore::~VaultStore()
{
    lock();
}

QString VaultStore::defaultPath()
{
    const QByteArray env = qgetenv("BURROW_VAULT");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("vault.json"));
}

bool VaultStore::exists() const
{
    return QFileInfo::exists(m_path);
}

VaultStore::Result VaultStore::create(const QString &password)
{
    lock();
    m_doc = Model::Document{};
    QByteArray path = m_path.toUtf8();
    QByteArray pw = password.toUtf8();
    QByteArray json = Model::toJson(m_doc);
    int err = 0;
    int h = NetcoreVaultCreate(path.data(), pw.data(), json.data(), &err);
    wipe(pw);
    if (h <= 0)
        return fromCode(err);
    m_handle = h;
    return Result::Ok;
}

VaultStore::Result VaultStore::open(const QString &password)
{
    lock();
    QByteArray path = m_path.toUtf8();
    QByteArray pw = password.toUtf8();
    int err = 0;
    int h = NetcoreVaultOpen(path.data(), pw.data(), &err);
    wipe(pw);
    return finishOpen(h, err);
}

VaultStore::Result VaultStore::finishOpen(int handle, int err)
{
    if (handle <= 0)
        return fromCode(err);
    char *raw = NetcoreVaultDocument(handle);
    QByteArray json(raw ? raw : "");
    NetcoreFreeSecret(raw);
    Model::Document doc;
    const bool ok = Model::fromJson(json, &doc);
    wipe(json);
    if (!ok) {
        NetcoreVaultClose(handle);
        return Result::BadSchema;
    }
    m_handle = handle;
    m_doc = doc;
    return Result::Ok;
}

VaultStore::Result VaultStore::save()
{
    if (!isOpen())
        return Result::IoError;
    QByteArray json = Model::toJson(m_doc);
    const int code = NetcoreVaultSave(m_handle, json.data());
    wipe(json);
    return fromCode(code);
}

VaultStore::Result VaultStore::changePassword(const QString &newPassword)
{
    if (!isOpen())
        return Result::IoError;
    QByteArray pw = newPassword.toUtf8();
    const int code = NetcoreVaultChangePassword(m_handle, pw.data());
    wipe(pw);
    return fromCode(code);
}

bool VaultStore::verifyPassword(const QString &password) const
{
    QByteArray path = m_path.toUtf8();
    QByteArray pw = password.toUtf8();
    int err = 0;
    const int h = NetcoreVaultOpen(path.data(), pw.data(), &err);
    wipe(pw);
    if (h > 0)
        NetcoreVaultClose(h);
    return h > 0;
}

void VaultStore::lock()
{
    if (m_handle > 0) {
        NetcoreVaultClose(m_handle);
        m_handle = 0;
    }
    // Overwrite secrets held in the decrypted document before dropping it.
    for (Model::Device &d : m_doc.devices)
        d.password.fill(QChar(0));
    for (Model::Tunnel &t : m_doc.tunnels) {
        t.gpPassword.fill(QChar(0));
        t.totp.secret.fill(QChar(0));
    }
    m_doc = Model::Document{};
}

QString VaultStore::message(Result r)
{
    switch (r) {
    case Result::Ok: return {};
    case Result::WrongPassword: return QCoreApplication::translate("VaultStore", "Wrong master password.");
    case Result::Corrupt:
        return QCoreApplication::translate("VaultStore",
            "The vault file is damaged or from an unsupported version. There may be a .bak backup next to it.");
    case Result::Exists: return QCoreApplication::translate("VaultStore", "A vault already exists.");
    case Result::BadSchema:
        return QCoreApplication::translate("VaultStore",
            "The vault opened, but this version can't read its contents. Update the app.");
    case Result::IoError: break;
    }
    return QCoreApplication::translate("VaultStore", "Couldn't read or write the vault file.");
}

bool VaultStore::putBlob(const QString &name, const QByteArray &data)
{
    if (!isOpen())
        return false;
    QByteArray n = name.toUtf8();
    return NetcoreVaultPutBlob(m_handle, n.data(), const_cast<char *>(data.constData()),
                               int(data.size())) == 0;
}

QByteArray VaultStore::blob(const QString &name) const
{
    if (!isOpen())
        return {};
    QByteArray n = name.toUtf8();
    int len = 0;
    void *p = NetcoreVaultBlob(m_handle, n.data(), &len);
    if (!p)
        return {};
    QByteArray out(static_cast<const char *>(p), len);
    NetcoreFreeBytes(p);
    return out;
}

void VaultStore::deleteBlob(const QString &name)
{
    if (!isOpen())
        return;
    QByteArray n = name.toUtf8();
    NetcoreVaultDeleteBlob(m_handle, n.data());
}
