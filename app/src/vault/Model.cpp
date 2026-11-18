#include "vault/Model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

namespace Model {

namespace {

QJsonObject totpToJson(const Totp &t)
{
    return {{"secret", t.secret}, {"algorithm", t.algorithm}, {"digits", t.digits},
            {"period", t.period}, {"issuer", t.issuer}, {"account", t.account}};
}

Totp totpFromJson(const QJsonObject &o)
{
    Totp t;
    t.secret = o.value("secret").toString();
    t.algorithm = o.value("algorithm").toString(t.algorithm);
    t.digits = o.value("digits").toInt(t.digits);
    t.period = o.value("period").toInt(t.period);
    t.issuer = o.value("issuer").toString();
    t.account = o.value("account").toString();
    return t;
}

QJsonObject tunnelToJsonImpl(const Tunnel &t)
{
    const char *type = t.type == Tunnel::Type::Tailscale ? "tailscale"
                     : t.type == Tunnel::Type::Cloudflare ? "cloudflare" : "globalprotect";
    QJsonObject o{{"id", t.id}, {"name", t.name}, {"type", type}};
    if (t.type == Tunnel::Type::GlobalProtect) {
        o["gp"] = QJsonObject{
            {"host", t.gpHost}, {"username", t.gpUsername},
            {"password", t.gpSavePassword ? t.gpPassword : QString()},
            {"savePassword", t.gpSavePassword}, {"authGroup", t.gpAuthGroup},
            {"otpEnabled", t.otpEnabled}, {"otpMode", t.otpMode},
            {"otpSource", t.otpSource}, {"totp", totpToJson(t.totp)}};
    } else if (t.type == Tunnel::Type::Tailscale) {
        o["tailscale"] = QJsonObject{{"controlUrl", t.tsControlUrl}, {"hostname", t.tsHostname}};
    } else {
        o["cloudflare"] = QJsonObject{{"bastion", t.cfBastion}};
    }
    return o;
}

Tunnel tunnelFromJsonImpl(const QJsonObject &o)
{
    Tunnel t;
    t.id = o.value("id").toString();
    t.name = o.value("name").toString();
    const QString type = o.value("type").toString();
    t.type = type == "tailscale" ? Tunnel::Type::Tailscale
           : type == "cloudflare" ? Tunnel::Type::Cloudflare : Tunnel::Type::GlobalProtect;
    const QJsonObject gp = o.value("gp").toObject();
    t.gpHost = gp.value("host").toString();
    t.gpUsername = gp.value("username").toString();
    t.gpSavePassword = gp.value("savePassword").toBool();
    t.gpPassword = t.gpSavePassword ? gp.value("password").toString() : QString();
    t.gpAuthGroup = gp.value("authGroup").toString();
    t.otpEnabled = gp.value("otpEnabled").toBool();
    t.otpMode = gp.value("otpMode").toString(t.otpMode);
    t.otpSource = gp.value("otpSource").toString(t.otpSource);
    t.totp = totpFromJson(gp.value("totp").toObject());
    const QJsonObject ts = o.value("tailscale").toObject();
    t.tsControlUrl = ts.value("controlUrl").toString();
    t.tsHostname = ts.value("hostname").toString();
    t.cfBastion = o.value("cloudflare").toObject().value("bastion").toString();
    return t;
}

QJsonObject deviceToJsonImpl(const Device &d)
{
    return {{"id", d.id}, {"name", d.name}, {"os", d.os}, {"protocol", d.protocol}, {"host", d.host}, {"port", d.port},
            {"username", d.username}, {"domain", d.domain},
            {"password", d.savePassword ? d.password : QString()},
            {"savePassword", d.savePassword}, {"autoLogin", d.autoLogin},
            {"tunnelId", d.tunnelId}, {"lockOnDisconnect", d.lockOnDisconnect},
            {"onDisconnect", d.onDisconnect}, {"sessionMode", d.sessionMode},
            {"clipboard", d.clipboard}, {"keyMap", d.keyMap}, {"audio", d.audio}, {"sharedFolder", d.sharedFolder}, {"sharePrinters", d.sharePrinters}, {"displays", d.displays}, {"retina", d.retina},
            {"layout", d.layout},
            {"certFingerprint", d.certFingerprint}};
}

Device deviceFromJsonImpl(const QJsonObject &o)
{
    Device d;
    d.id = o.value("id").toString();
    d.name = o.value("name").toString();
    d.os = o.value("os").toString(d.os);
    d.protocol = o.value("protocol").toString(d.protocol) == QLatin1String("vnc") ? QStringLiteral("vnc") : QStringLiteral("rdp");
    d.host = o.value("host").toString();
    d.port = o.value("port").toInt(d.port);
    d.username = o.value("username").toString();
    d.domain = o.value("domain").toString();
    d.savePassword = o.value("savePassword").toBool();
    d.password = d.savePassword ? o.value("password").toString() : QString();
    d.autoLogin = o.value("autoLogin").toBool();
    d.tunnelId = o.value("tunnelId").toString();
    d.lockOnDisconnect = o.value("lockOnDisconnect").toBool(d.lockOnDisconnect);
    d.onDisconnect = o.value("onDisconnect").toString(d.onDisconnect);
    d.sessionMode = o.value("sessionMode").toString(d.sessionMode);
    d.clipboard = o.value("clipboard").toString(d.clipboard);
    d.keyMap = o.value("keyMap").toString(d.keyMap);
    d.audio = o.value("audio").toString(d.audio);
    d.sharedFolder = o.value("sharedFolder").toString();
    d.sharePrinters = o.value("sharePrinters").toBool();
    d.displays = o.value("displays").toString(d.displays);
    d.retina = o.value("retina").toBool(d.retina);
    d.layout = o.value("layout").toObject();
    d.certFingerprint = o.value("certFingerprint").toString();
    return d;
}

} // namespace

const Tunnel *Document::tunnel(const QString &id) const
{
    for (const Tunnel &t : tunnels)
        if (t.id == id)
            return &t;
    return nullptr;
}

QJsonObject toJson(const Device &d) { return deviceToJsonImpl(d); }
QJsonObject toJson(const Tunnel &t) { return tunnelToJsonImpl(t); }
Device deviceFromJson(const QJsonObject &o) { return deviceFromJsonImpl(o); }
Tunnel tunnelFromJson(const QJsonObject &o) { return tunnelFromJsonImpl(o); }

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QByteArray toJson(const Document &doc)
{
    QJsonArray devices, tunnels;
    for (const Device &d : doc.devices)
        devices.append(deviceToJsonImpl(d));
    for (const Tunnel &t : doc.tunnels)
        tunnels.append(tunnelToJsonImpl(t));
    QJsonObject root{{"schema", kSchemaVersion}, {"devices", devices}, {"tunnels", tunnels},
                     {"settings", QJsonObject{{"autoLockMinutes", doc.settings.autoLockMinutes},
                                              {"multiMonitorDefault", true}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool fromJson(const QByteArray &json, Document *out)
{
    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject())
        return false;
    const QJsonObject root = jd.object();
    const int schema = root.value("schema").toInt(-1);
    if (schema < 1 || schema > kSchemaVersion)
        return false;
    Document doc;
    for (const QJsonValue &v : root.value("devices").toArray())
        doc.devices.append(deviceFromJsonImpl(v.toObject()));
    for (const QJsonValue &v : root.value("tunnels").toArray())
        doc.tunnels.append(tunnelFromJsonImpl(v.toObject()));
    const QJsonObject s = root.value("settings").toObject();
    doc.settings.autoLockMinutes = s.value("autoLockMinutes").toInt(15);
    // One-time switch (2026-11-15): every device starts with one monitor per Mac display. Vaults
    // written before that have no flag; their devices still carry the old single-window default.
    if (!s.contains("multiMonitorDefault"))
        for (Device &d : doc.devices)
            d.displays = QStringLiteral("all");
    *out = doc;
    return true;
}

} // namespace Model
