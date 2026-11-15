#pragma once

#include <QByteArray>
#include <QString>

// Stores the vault data key behind biometric authentication (ADR 0004).
// macOS: Data Protection keychain item with kSecAccessControlBiometryCurrentSet.
// Other platforms: not available yet.
namespace Biometric {

// True if the device has enrolled biometrics (Touch ID).
bool isAvailable();

// Stores key, replacing any previous item. On failure returns false and sets
// *error to a user-facing message (e.g. unsigned build lacks the entitlement).
bool storeKey(const QByteArray &key, QString *error);

// Prompts for Touch ID and returns the key, or an empty array if cancelled,
// failed, or no key is stored. Blocks until the prompt is answered.
QByteArray loadKey(const QString &reason, QString *error);

void removeKey();

} // namespace Biometric
