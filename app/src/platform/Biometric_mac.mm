#include "platform/Biometric.h"

#include <QCoreApplication>

#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

namespace {

NSString *const kService = @"io.github.metin-aydin.burrow.vault";
NSString *const kAccount = @"vault-key";

NSMutableDictionary *baseQuery()
{
    return [@{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: kService,
        (__bridge id)kSecAttrAccount: kAccount,
        (__bridge id)kSecUseDataProtectionKeychain: @YES,
    } mutableCopy];
}

QString statusMessage(OSStatus st)
{
    if (st == errSecMissingEntitlement)
        return QCoreApplication::translate("Biometric", "Touch ID isn't available in this build (requires a signed release).");
    if (st == errSecUserCanceled || st == errSecAuthFailed)
        return QCoreApplication::translate("Biometric", "Touch ID was not confirmed.");
    if (st == errSecItemNotFound)
        return QCoreApplication::translate("Biometric", "No key is saved for Touch ID; unlock with your master password.");
    CFStringRef s = SecCopyErrorMessageString(st, nullptr);
    QString msg = s ? QString::fromCFString(s) : QString::number(st);
    if (s)
        CFRelease(s);
    return QCoreApplication::translate("Biometric", "Keychain error: %1").arg(msg);
}

} // namespace

namespace Biometric {

bool isAvailable()
{
    @autoreleasepool {
        LAContext *ctx = [[LAContext alloc] init];
        return [ctx canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:nil];
    }
}

bool storeKey(const QByteArray &key, QString *error)
{
    @autoreleasepool {
        removeKey();
        CFErrorRef cfErr = nullptr;
        SecAccessControlRef access = SecAccessControlCreateWithFlags(
            nullptr, kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
            kSecAccessControlBiometryCurrentSet, &cfErr);
        if (!access) {
            if (error)
                *error = QCoreApplication::translate("Biometric", "Couldn't create the access control.");
            if (cfErr)
                CFRelease(cfErr);
            return false;
        }
        NSMutableDictionary *q = baseQuery();
        q[(__bridge id)kSecAttrAccessControl] = (__bridge_transfer id)access;
        q[(__bridge id)kSecValueData] = [NSData dataWithBytes:key.constData() length:key.size()];
        const OSStatus st = SecItemAdd((__bridge CFDictionaryRef)q, nullptr);
        if (st != errSecSuccess) {
            if (error)
                *error = statusMessage(st);
            return false;
        }
        return true;
    }
}

QByteArray loadKey(const QString &reason, QString *error)
{
    @autoreleasepool {
        LAContext *ctx = [[LAContext alloc] init];
        ctx.localizedReason = reason.toNSString();
        NSMutableDictionary *q = baseQuery();
        q[(__bridge id)kSecReturnData] = @YES;
        q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        q[(__bridge id)kSecUseAuthenticationContext] = ctx;
        CFTypeRef result = nullptr;
        const OSStatus st = SecItemCopyMatching((__bridge CFDictionaryRef)q, &result);
        if (st != errSecSuccess) {
            if (error)
                *error = statusMessage(st);
            return {};
        }
        NSData *data = (__bridge_transfer NSData *)result;
        return QByteArray(static_cast<const char *>(data.bytes), static_cast<int>(data.length));
    }
}

void removeKey()
{
    @autoreleasepool {
        SecItemDelete((__bridge CFDictionaryRef)baseQuery());
    }
}

} // namespace Biometric
