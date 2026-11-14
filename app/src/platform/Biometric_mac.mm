#include "platform/Biometric.h"

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
        return QStringLiteral("Touch ID bu build'de kullanılamıyor (imzalı sürüm gerekir).");
    if (st == errSecUserCanceled || st == errSecAuthFailed)
        return QStringLiteral("Touch ID doğrulaması yapılmadı.");
    if (st == errSecItemNotFound)
        return QStringLiteral("Touch ID için kayıtlı anahtar yok; ana şifreyle aç.");
    CFStringRef s = SecCopyErrorMessageString(st, nullptr);
    QString msg = s ? QString::fromCFString(s) : QString::number(st);
    if (s)
        CFRelease(s);
    return QStringLiteral("Keychain hatası: %1").arg(msg);
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
                *error = QStringLiteral("Erişim kontrolü oluşturulamadı.");
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
