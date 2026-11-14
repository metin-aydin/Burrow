#include "app/PreviewProvider.h"

#include "vault/VaultStore.h"

QImage PreviewProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    const QString deviceId = id.section(QLatin1Char('?'), 0, 0);
    QByteArray data = m_store->blob(blobName(deviceId));
    QImage img = QImage::fromData(data);
    std::fill(data.begin(), data.end(), '\0');
    if (img.isNull())
        return {};
    if (requested.isValid() && !requested.isEmpty())
        img = img.scaled(requested, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (size)
        *size = img.size();
    return img;
}
