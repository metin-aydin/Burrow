#pragma once

#include <QImage>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QTranslator>
#include <QVariantMap>

#include "app/ListModels.h"
#include "app/TunnelManager.h"
#include "app/Updater.h"
#include "rdp/RdpSession.h"
#include "vault/VaultStore.h"

// The single object QML talks to (exposed as the `App` singleton). Owns the
// vault and exposes devices / tunnels as models plus edit operations that take
// and return plain maps. Methods returning QString return "" on success and a
// user-facing error otherwise.
class QQmlEngine;

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)   // setup | locked | unlocked
    Q_PROPERTY(DeviceModel *devices READ devices CONSTANT)
    Q_PROPERTY(TunnelModel *tunnels READ tunnels CONSTANT)
    Q_PROPERTY(DeviceFilterModel *gallery READ gallery CONSTANT)
    Q_PROPERTY(Updater *updater READ updater CONSTANT)
    Q_PROPERTY(QVariantMap tunnelStates READ tunnelStates NOTIFY tunnelStatesChanged)
    Q_PROPERTY(bool touchIdAvailable READ touchIdAvailable CONSTANT)
    Q_PROPERTY(bool touchIdEnabled READ touchIdEnabled NOTIFY touchIdEnabledChanged)
    Q_PROPERTY(QString versionInfo READ versionInfo CONSTANT)
    Q_PROPERTY(int autoLockMinutes READ autoLockMinutes WRITE setAutoLockMinutes NOTIFY autoLockChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)   // system | en | tr
    Q_PROPERTY(QString demoOpen MEMBER m_demoOpen CONSTANT)   // dev: dialog to open on start
public:
    explicit AppController(QObject *parent = nullptr);

    QString state() const;
    DeviceModel *devices() { return &m_devices; }
    TunnelModel *tunnels() { return &m_tunnels; }
    DeviceFilterModel *gallery() { return &m_gallery; }
    Updater *updater() { return &m_updater; }
    QVariantMap tunnelStates() const { return m_tunnelMgr.states(); }
    bool touchIdAvailable() const;
    bool touchIdEnabled() const;
    QString versionInfo() const;
    VaultStore *store() { return &m_store; }

    // --- Language ----------------------------------------------------------
    int autoLockMinutes() const { return m_store.isOpen() ? m_store.doc().settings.autoLockMinutes : 15; }
    void setAutoLockMinutes(int minutes);
    // Called for every user input event; restarts the inactivity timer.
    void noteActivity();
    QString language() const;
    void setLanguage(const QString &lang);
    // Installs the translator for the saved (or system) language. Call before loading QML.
    void applyLanguage(QQmlEngine *engine = nullptr);

    // --- Vault -------------------------------------------------------------
    Q_INVOKABLE QString createVault(const QString &password, const QString &confirm);
    Q_INVOKABLE QString unlock(const QString &password);
    Q_INVOKABLE QString unlockWithTouchId();
    Q_INVOKABLE void lock();
    // Closes all sessions (saving their previews) and locks the vault. Call before exiting.
    void shutdown();
    Q_INVOKABLE QString changePassword(const QString &current, const QString &next,
                                       const QString &again);
    Q_INVOKABLE QString setTouchIdEnabled(bool on);

    // --- Devices -----------------------------------------------------------
    Q_INVOKABLE QVariantMap newDevice() const;
    Q_INVOKABLE QVariantMap device(const QString &id) const;
    Q_INVOKABLE QString saveDevice(const QVariantMap &map);
    Q_INVOKABLE void duplicateDevice(const QString &id);
    Q_INVOKABLE void deleteDevice(const QString &id);
    // Returns "" when a session was started, "credentials" when username/password must be asked
    // first, or an error message.
    Q_INVOKABLE QString connectDevice(const QString &id, const QString &username = {},
                                      const QString &password = {});
    Q_INVOKABLE bool needsCredentials(const QString &id) const;
    // Tunnels
    Q_INVOKABLE QString setTunnelPassword(const QString &tunnelId, const QString &password);
    Q_INVOKABLE void submitTunnelOtp(const QString &tunnelId, const QString &code);
    Q_INVOKABLE void connectTunnel(const QString &tunnelId);
    Q_INVOKABLE void disconnectTunnel(const QString &tunnelId);
    Q_INVOKABLE QString tunnelName(const QString &tunnelId) const;
    // Test a (possibly unsaved) profile from the editor: starts the tunnel and reports progress in
    // tunnelStates[id]. `target` is a device address for Cloudflare. Returns the id used or an error.
    Q_INVOKABLE QVariantMap testTunnel(const QVariantMap &profile, const QString &password, const QString &target);
    Q_INVOKABLE void stopTunnelTest(const QString &tunnelId);
    // Import devices from a tunnel's network: Tailscale peers (tunnel must be connected) or Cloudflare
    // Access apps listed with an API token (asynchronous: cloudflareAppsReady). importDevices returns
    // how many were added.
    Q_INVOKABLE void openTunnelForImport(const QString &tunnelId);
    Q_INVOKABLE QVariantList tunnelPeers(const QString &tunnelId) const;
    Q_INVOKABLE void listCloudflareApps(const QString &apiToken);
    Q_INVOKABLE int importDevices(const QString &tunnelId, const QVariantList &items);
    Q_INVOKABLE QVariantMap deviceTunnel(const QString &deviceId) const;   // {id, name, type} or {}
    Q_INVOKABLE void forgetCertificate(const QString &id);
    // Stops waiting for a device's tunnel (its window was closed while connecting).
    Q_INVOKABLE void cancelConnect(const QString &deviceId);
    // Logical size a session window opens with on the main display.
    Q_INVOKABLE QSize sessionWindowSize() const;

    // Stores a preview image for a device (called by the RDP session on close).
    void setPreview(const QString &deviceId, const QImage &image);

    // --- Tunnels -----------------------------------------------------------
    Q_INVOKABLE QVariantMap newTunnel() const;
    Q_INVOKABLE QVariantMap tunnel(const QString &id) const;
    Q_INVOKABLE QString saveTunnel(const QVariantMap &map);
    Q_INVOKABLE void deleteTunnel(const QString &id);
    Q_INVOKABLE QVariantList tunnelChoices() const;   // [{id, name, type}] incl. direct

    // --- TOTP --------------------------------------------------------------
    Q_INVOKABLE QVariantList parseTotp(const QString &uri) const;
    Q_INVOKABLE QVariantMap totpCode(const QVariantMap &totp) const;   // {code, remaining}
    // QR codes (authenticator exports): return the decoded text or "".
    Q_INVOKABLE QString decodeQrImage(const QVariant &image) const;   // QImage (e.g. a camera frame grab)
    Q_INVOKABLE QString decodeQrFile(const QUrl &file) const;
    Q_INVOKABLE QString decodeQrClipboard() const;
    Q_INVOKABLE QString clipboardText() const;

    // Development helper: adds sample devices with generated previews.
    void seedDemo();
    void setDemoOpen(const QString &which) { m_demoOpen = which; }

signals:
    void stateChanged();
    void touchIdEnabledChanged();
    void toast(const QString &message);
    void languageChanged();
    void autoLockChanged();
    void sessionOpened(RdpSession *session);
    void tunnelStatesChanged();
    void tunnelLoginRequired(const QString &tunnelId, const QString &name, const QString &url);
    void tunnelOtpRequired(const QString &tunnelId, const QString &name, const QString &prompt);
    void tunnelPasswordRequired(const QString &tunnelId, const QString &name);
    void deviceConnecting(const QString &deviceId, const QString &tunnelId);   // waiting for the tunnel
    void deviceConnectFailed(const QString &deviceId, const QString &error);
    void cloudflareAppsReady(const QVariantMap &result);   // {apps: [...]} or {error}

private:
    QString finishUnlock(VaultStore::Result r);
    QString persist();
    void reloadModels();
    QString blobPath(const QString &name) const;
    QString startSession(const Model::Device &dev, const QString &user, const QString &pass,
                         const QString &tunnelId);
    void onTunnelState(const QString &tunnelId, const QString &state, const QString &error);
    void stopTunnelIfIdle(const QString &tunnelId);
    struct Pending { QString deviceId, username, password, tunnelId; };

    VaultStore m_store;
    DeviceModel m_devices;
    TunnelModel m_tunnels;
    DeviceFilterModel m_gallery;
    Updater m_updater;
    TunnelManager m_tunnelMgr{&m_store};
    QList<Pending> m_pending;
    QHash<RdpSession *, QString> m_sessionTunnels;   // session -> tunnel it runs through
    QSet<QString> m_tunnelTesting;
    QTimer m_idleTimer;                    // tunnels opened by "Test Connection"
    QString m_demoOpen;
    QList<RdpSession *> m_sessions;
    QTranslator m_translator;
    QQmlEngine *m_engine = nullptr;
};
