#pragma once

#include <QList>
#include <QString>

#include "vault/Model.h"

// Thin C++ wrapper over the Go netcore C API (libnetcore.h).
namespace NetCore {

QString version();

// Parses an otpauth:// or otpauth-migration:// URI (may hold several accounts).
QList<Model::Totp> parseTotp(const QString &uri);

// Current TOTP code, or empty on bad parameters. *remaining = seconds left.
QString totpCode(const Model::Totp &t, int *remaining = nullptr);

// A loopback port forward. Closing it (or destroying it) tears down the
// tunnel-side connection.
class Forward
{
public:
    Forward() = default;
    ~Forward();
    Forward(const Forward &) = delete;
    Forward &operator=(const Forward &) = delete;

    // Direct (no tunnel) forward, used by the "direct" connection profile.
    bool startDirect(const QString &target);
    void stop();

    bool isActive() const { return m_handle > 0; }
    QString localAddress() const { return m_localAddress; }

private:
    int m_handle = 0;
    QString m_localAddress;
};

} // namespace NetCore
