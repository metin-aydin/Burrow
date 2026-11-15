#pragma once

#include <QString>

#include "vault/Model.h"

// Owns the unlocked vault (Go netcore, ADR 0004) and the decrypted document.
class VaultStore
{
public:
    enum class Result { Ok, WrongPassword, Corrupt, Exists, IoError, BadSchema };

    VaultStore();
    ~VaultStore();
    VaultStore(const VaultStore &) = delete;
    VaultStore &operator=(const VaultStore &) = delete;

    static QString defaultPath();

    QString path() const { return m_path; }
    bool exists() const;
    bool isOpen() const { return m_handle > 0; }
    int handle() const { return m_handle; }   // for netcore APIs that keep state in the vault

    Result create(const QString &password);
    Result open(const QString &password);
    Result save();
    Result changePassword(const QString &newPassword);
    // Checks a password against the file without touching the open vault.
    bool verifyPassword(const QString &password) const;
    void lock();

    // Encrypted side files (device previews, ADR 0005). Thread-safe.
    bool putBlob(const QString &name, const QByteArray &data);
    QByteArray blob(const QString &name) const;
    void deleteBlob(const QString &name);

    Model::Document &doc() { return m_doc; }
    const Model::Document &doc() const { return m_doc; }

    static QString message(Result r);

private:
    Result finishOpen(int handle, int err);

    QString m_path;
    int m_handle = 0;
    Model::Document m_doc;
};
