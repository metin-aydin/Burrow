#include "platform/Biometric.h"

// Windows Hello / Linux equivalents come later (ADR 0004).
namespace Biometric {

bool isAvailable() { return false; }

bool storeKey(const QByteArray &, QString *error)
{
    if (error)
        *error = QStringLiteral("Bu platformda biyometrik kilit henüz yok.");
    return false;
}

QByteArray loadKey(const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("Bu platformda biyometrik kilit henüz yok.");
    return {};
}

void removeKey() {}

} // namespace Biometric
