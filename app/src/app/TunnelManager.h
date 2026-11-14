#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include "vault/Model.h"

class VaultStore;

// Runs the in-app tunnels (Go netcore: tsnet / libopenconnect + userspace stack) and the
// one-shot loopback forwards RDP sessions use through them. Nothing here changes the system's
// network configuration.
class TunnelManager : public QObject
{
    Q_OBJECT
public:
    explicit TunnelManager(VaultStore *store, QObject *parent = nullptr);
    ~TunnelManager() override;

    // Starts the tunnel unless it is already running. `password` overrides a profile without a
    // saved password (kept in memory for this app session only). Returns "" or an error.
    // `target` (host:port) lets a tunnel prepare for a device (Cloudflare: sign in for its Access app).
    QString ensure(const Model::Tunnel &t, const QString &password = {}, const QString &target = {});
    bool needsPassword(const Model::Tunnel &t) const;

    QVariantMap status(const QString &tunnelId) const;   // {state, error, authUrl, address, prompt}
    QVariantMap states() const { return m_states; }
    bool isConnected(const QString &tunnelId) const;
    int handle(const QString &tunnelId) const { return m_handles.value(tunnelId); }

    // Opens a one-shot forward to `target` through the tunnel; returns "127.0.0.1:port" or "".
    QString openForward(const QString &tunnelId, const QString &target, int *forwardHandle);
    static void closeForward(int forwardHandle);
    static void sealForward(int forwardHandle);   // stop accepting once the session is connected

    void submitOtp(const QString &tunnelId, const QString &code);
    void stop(const QString &tunnelId);
    void stopAll();

signals:
    void statesChanged();
    void stateChanged(const QString &tunnelId, const QString &state, const QString &error);
    void loginRequired(const QString &tunnelId, const QString &url);
    void otpRequired(const QString &tunnelId, const QString &prompt);

private:
    void poll();

    VaultStore *m_store;
    QTimer m_timer;
    QHash<QString, int> m_handles;          // tunnelId -> netcore handle
    QHash<QString, QString> m_passwords;    // session-only VPN passwords
    QVariantMap m_states;                   // tunnelId -> status map
};
