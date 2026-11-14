#include "app/ListModels.h"

#include "vault/VaultStore.h"

namespace {
QString tunnelType(const Model::Tunnel &t)
{
    return t.type == Model::Tunnel::Type::Tailscale ? QStringLiteral("tailscale")
                                                    : QStringLiteral("globalprotect");
}
} // namespace

DeviceModel::DeviceModel(VaultStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_store->isOpen() ? 0 : int(m_store->doc().devices.size());
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const Model::Device &d = m_store->doc().devices.at(index.row());
    const Model::Tunnel *t = m_store->doc().tunnel(d.tunnelId);
    switch (role) {
    case IdRole: return d.id;
    case NameRole: return d.name;
    case HostRole: return d.host;
    case PortRole: return d.port;
    case OsRole: return d.os;
    case TunnelNameRole: return t ? t->name : QString();
    case TunnelTypeRole: return t ? tunnelType(*t) : QStringLiteral("direct");
    case TunnelIdRole: return t ? t->id : QString();
    case PreviewRole: {
        const int rev = m_previewRev.value(d.id);
        return rev ? QStringLiteral("image://preview/%1?v=%2").arg(d.id).arg(rev) : QString();
    }
    }
    return {};
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {{IdRole, "deviceId"}, {NameRole, "name"}, {HostRole, "host"}, {PortRole, "port"},
            {OsRole, "os"}, {TunnelNameRole, "tunnelName"}, {TunnelTypeRole, "tunnelType"},
            {PreviewRole, "preview"}, {TunnelIdRole, "tunnelId"}};
}

void DeviceModel::reload()
{
    beginResetModel();
    endResetModel();
    emit countChanged();
}

void DeviceModel::previewChanged(const QString &id, bool exists)
{
    if (exists)
        m_previewRev[id] = m_previewRev.value(id) + 1;
    else
        m_previewRev.remove(id);
    const auto &devices = m_store->doc().devices;
    for (int i = 0; i < devices.size(); ++i)
        if (devices.at(i).id == id)
            emit dataChanged(index(i), index(i), {PreviewRole});
}

// ---------------------------------------------------------------------------

TunnelModel::TunnelModel(VaultStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
}

int TunnelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_store->isOpen() ? 0 : int(m_store->doc().tunnels.size());
}

QVariant TunnelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const Model::Tunnel &t = m_store->doc().tunnels.at(index.row());
    switch (role) {
    case IdRole: return t.id;
    case NameRole: return t.name;
    case TypeRole: return tunnelType(t);
    case HostRole:
        return t.type == Model::Tunnel::Type::Tailscale
            ? (t.tsControlUrl.isEmpty() ? QStringLiteral("tailscale.com") : t.tsControlUrl)
            : t.gpHost;
    case UsersRole: {
        int n = 0;
        for (const Model::Device &d : m_store->doc().devices)
            n += d.tunnelId == t.id;
        return n;
    }
    }
    return {};
}

QHash<int, QByteArray> TunnelModel::roleNames() const
{
    return {{IdRole, "tunnelId"}, {NameRole, "name"}, {TypeRole, "type"}, {HostRole, "host"},
            {UsersRole, "users"}};
}

void TunnelModel::reload()
{
    beginResetModel();
    endResetModel();
    emit countChanged();
}

// ---------------------------------------------------------------------------

DeviceFilterModel::DeviceFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    auto emitCount = [this] { emit countChanged(); };
    connect(this, &QAbstractItemModel::modelReset, this, emitCount);
    connect(this, &QAbstractItemModel::rowsInserted, this, emitCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, emitCount);
}

void DeviceFilterModel::setQuery(const QString &q)
{
    if (q == m_query)
        return;
    beginFilterChange();
    m_query = q;
    endFilterChange();
    emit changed();
    emit countChanged();
}

void DeviceFilterModel::setTunnel(const QString &t)
{
    if (t == m_tunnel)
        return;
    beginFilterChange();
    m_tunnel = t;
    endFilterChange();
    emit changed();
    emit countChanged();
}

bool DeviceFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (m_tunnel != QLatin1String("*")
        && i.data(DeviceModel::TunnelIdRole).toString() != m_tunnel)
        return false;
    if (m_query.isEmpty())
        return true;
    return i.data(DeviceModel::NameRole).toString().contains(m_query, Qt::CaseInsensitive)
        || i.data(DeviceModel::HostRole).toString().contains(m_query, Qt::CaseInsensitive);
}
