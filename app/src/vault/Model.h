#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

// Vault document schema (ADR 0004). Serialized as JSON inside the encrypted
// vault; never written to disk in plaintext.
namespace Model {

constexpr int kSchemaVersion = 1;

struct Totp
{
    QString secret;          // base32
    QString algorithm = QStringLiteral("SHA1");
    int digits = 6;
    int period = 30;
    QString issuer;
    QString account;

    bool isSet() const { return !secret.isEmpty(); }
};

// A tunnel profile (SPEC §3). Devices reference it by id.
struct Tunnel
{
    enum class Type { GlobalProtect, Tailscale, Cloudflare };

    QString id;
    QString name;
    Type type = Type::GlobalProtect;

    // GlobalProtect
    QString gpHost;
    QString gpUsername;
    QString gpPassword;      // only kept when savePassword
    bool gpSavePassword = false;
    QString gpAuthGroup;
    bool otpEnabled = false;
    QString otpMode = QStringLiteral("append");     // append | separate
    QString otpSource = QStringLiteral("totp");     // totp | manual
    Totp totp;

    // Tailscale
    QString tsControlUrl;    // empty = default control plane
    QString tsHostname;

    // Cloudflare (ADR 0007): empty bastion = each device's host is its Access application
    QString cfBastion;
};

// A saved remote computer (SPEC §4, §4.1).
struct Device
{
    QString id;
    QString name;
    QString os = QStringLiteral("windows");          // windows | ubuntu
    QString host;
    int port = 3389;
    QString username;
    QString domain;
    QString password;        // only kept when savePassword
    bool savePassword = false;
    bool autoLogin = false;
    QString tunnelId;        // empty = direct
    bool lockOnDisconnect = true;
    QString onDisconnect = QStringLiteral("keep_locked");  // keep_locked | sign_out
    QString sessionMode = QStringLiteral("existing");      // existing | new | ask
    QString clipboard = QStringLiteral("both");            // both | off | to_remote | to_local
    QString keyMap = QStringLiteral("shortcuts");          // shortcuts | swap | off
    QString displays = QStringLiteral("all");              // all (one monitor per Mac display, as tabs) | window
    bool retina = false;     // send the Mac's physical pixels (sharper, but needs remote scaling)
    QJsonObject layout;      // last multi-monitor layout (SPEC §5.1)
    QString certFingerprint; // TOFU pin of the RDP server certificate
};

struct Settings
{
    bool touchId = false;
    int autoLockMinutes = 15; // lock after this much inactivity (no open session); 0 = never
};

struct Document
{
    QList<Device> devices;
    QList<Tunnel> tunnels;
    Settings settings;

    const Tunnel *tunnel(const QString &id) const;
};

QString newId();
QJsonObject toJson(const Device &d);
QJsonObject toJson(const Tunnel &t);
Device deviceFromJson(const QJsonObject &o);
Tunnel tunnelFromJson(const QJsonObject &o);
QByteArray toJson(const Document &doc);
// Returns false if json is not a valid document of a supported schema.
bool fromJson(const QByteArray &json, Document *out);

} // namespace Model
