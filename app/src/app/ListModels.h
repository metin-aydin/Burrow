#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QHash>

class VaultStore;

// Read-only views of the vault document for QML. Lists are small, so any
// change simply resets the model.
class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role {
        IdRole = Qt::UserRole + 1, NameRole, HostRole, PortRole, OsRole,
        TunnelNameRole, TunnelTypeRole, PreviewRole, TunnelIdRole
    };
    explicit DeviceModel(VaultStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    void reload();
    // Bumps a device's preview revision so QML reloads the image.
    void previewChanged(const QString &id, bool exists);
    void setPreviewRevisions(QHash<QString, int> revs) { m_previewRev = std::move(revs); }

signals:
    void countChanged();

private:
    VaultStore *m_store;
    QHash<QString, int> m_previewRev; // 0 or missing = no preview
};

class TunnelModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, TypeRole, HostRole, UsersRole };
    explicit TunnelModel(VaultStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    void reload();

signals:
    void countChanged();

private:
    VaultStore *m_store;
};

// Search + tunnel filter over DeviceModel for the gallery.
class DeviceFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(QString tunnel READ tunnel WRITE setTunnel NOTIFY changed)   // "*" = all, "" = direct
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit DeviceFilterModel(QObject *parent = nullptr);
    QString query() const { return m_query; }
    QString tunnel() const { return m_tunnel; }
    void setQuery(const QString &q);
    void setTunnel(const QString &t);
    int count() const { return rowCount(); }

signals:
    void changed();
    void countChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    QString m_query;
    QString m_tunnel = QStringLiteral("*");
};
