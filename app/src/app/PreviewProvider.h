#pragma once

#include <QQuickImageProvider>

class VaultStore;

// Serves image://preview/<deviceId>?v=<rev> from encrypted vault blobs.
class PreviewProvider : public QQuickImageProvider
{
public:
    explicit PreviewProvider(VaultStore *store)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_store(store)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

    static QString blobName(const QString &deviceId) { return QStringLiteral("preview-") + deviceId; }

private:
    VaultStore *m_store;
};
