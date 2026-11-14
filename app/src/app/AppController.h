#pragma once

#include <QImage>
#include <QObject>
#include <QVariantMap>

#include "app/ListModels.h"
#include "vault/VaultStore.h"

// The single object QML talks to (exposed as the `App` singleton). Owns the
// vault and exposes devices / tunnels as models plus edit operations that take
// and return plain maps. Methods returning QString return "" on success and a
// user-facing error otherwise.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)   // setup | locked | unlocked
    Q_PROPERTY(DeviceModel *devices READ devices CONSTANT)
    Q_PROPERTY(TunnelModel *tunnels READ tunnels CONSTANT)
    Q_PROPERTY(DeviceFilterModel *gallery READ gallery CONSTANT)
    Q_PROPERTY(bool touchIdAvailable READ touchIdAvailable CONSTANT)
    Q_PROPERTY(bool touchIdEnabled READ touchIdEnabled NOTIFY touchIdEnabledChanged)
    Q_PROPERTY(QString versionInfo READ versionInfo CONSTANT)
    Q_PROPERTY(QString demoOpen MEMBER m_demoOpen CONSTANT)   // dev: dialog to open on start
public:
    explicit AppController(QObject *parent = nullptr);

    QString state() const;
    DeviceModel *devices() { return &m_devices; }
    TunnelModel *tunnels() { return &m_tunnels; }
    DeviceFilterModel *gallery() { return &m_gallery; }
    bool touchIdAvailable() const;
    bool touchIdEnabled() const;
    QString versionInfo() const;
    VaultStore *store() { return &m_store; }

    // --- Vault -------------------------------------------------------------
    Q_INVOKABLE QString createVault(const QString &password, const QString &confirm);
    Q_INVOKABLE QString unlock(const QString &password);
    Q_INVOKABLE QString unlockWithTouchId();
    Q_INVOKABLE void lock();
    Q_INVOKABLE QString changePassword(const QString &current, const QString &next,
                                       const QString &again);
    Q_INVOKABLE QString setTouchIdEnabled(bool on);

    // --- Devices -----------------------------------------------------------
    Q_INVOKABLE QVariantMap newDevice() const;
    Q_INVOKABLE QVariantMap device(const QString &id) const;
    Q_INVOKABLE QString saveDevice(const QVariantMap &map);
    Q_INVOKABLE void duplicateDevice(const QString &id);
    Q_INVOKABLE void deleteDevice(const QString &id);
    Q_INVOKABLE void connectDevice(const QString &id);

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

    // Development helper: adds sample devices with generated previews.
    void seedDemo();
    void setDemoOpen(const QString &which) { m_demoOpen = which; }

signals:
    void stateChanged();
    void touchIdEnabledChanged();
    void toast(const QString &message);

private:
    QString finishUnlock(VaultStore::Result r);
    QString persist();
    void reloadModels();
    QString blobPath(const QString &name) const;

    VaultStore m_store;
    DeviceModel m_devices;
    TunnelModel m_tunnels;
    DeviceFilterModel m_gallery;
    QString m_demoOpen;
};
