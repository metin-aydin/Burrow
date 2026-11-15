#include "platform/Biometric.h"

#include <QCoreApplication>

// Windows Hello / Linux equivalents come later (ADR 0004).
namespace Biometric {

bool isAvailable() { return false; }

bool storeKey(const QByteArray &, QString *error)
{
    if (error)
        *error = QCoreApplication::translate("Biometric", "Biometric unlock isn't available on this platform yet.");
    return false;
}

QByteArray loadKey(const QString &, QString *error)
{
    if (error)
        *error = QCoreApplication::translate("Biometric", "Biometric unlock isn't available on this platform yet.");
    return {};
}

void removeKey() {}

} // namespace Biometric
