#include "net/NetCore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <libnetcore.h>

namespace NetCore {

namespace {
QString takeString(char *p)
{
    if (!p)
        return {};
    QString s = QString::fromUtf8(p);
    NetcoreFree(p);
    return s;
}
} // namespace

QString version()
{
    return takeString(NetcoreVersion());
}

QList<Model::Totp> parseTotp(const QString &uri)
{
    QByteArray u = uri.trimmed().toUtf8();
    char *raw = NetcoreTOTPParse(u.data());
    QByteArray json(raw ? raw : "");
    NetcoreFreeSecret(raw);
    QList<Model::Totp> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        Model::Totp t;
        t.secret = o.value("secret").toString();
        t.algorithm = o.value("algorithm").toString(QStringLiteral("SHA1"));
        t.digits = o.value("digits").toInt(6);
        t.period = o.value("period").toInt(30);
        t.issuer = o.value("issuer").toString();
        t.account = o.value("account").toString();
        out.append(t);
    }
    std::fill(json.begin(), json.end(), '\0');
    return out;
}

QString totpCode(const Model::Totp &t, int *remaining)
{
    QByteArray secret = t.secret.toUtf8();
    QByteArray alg = t.algorithm.toUtf8();
    int left = 0;
    char *raw = NetcoreTOTPCode(secret.data(), alg.data(), t.digits, t.period, &left);
    std::fill(secret.begin(), secret.end(), '\0');
    if (remaining)
        *remaining = left;
    if (!raw)
        return {};
    QString code = QString::fromUtf8(raw);
    NetcoreFreeSecret(raw);
    return code;
}

Forward::~Forward()
{
    stop();
}

bool Forward::startDirect(const QString &target)
{
    stop();
    QByteArray t = target.toUtf8();
    char *addr = nullptr;
    int h = NetcoreStartDirectForward(t.data(), &addr);
    if (h <= 0)
        return false;
    m_handle = h;
    m_localAddress = takeString(addr);
    return true;
}

void Forward::stop()
{
    if (m_handle > 0) {
        NetcoreStopForward(m_handle);
        m_handle = 0;
        m_localAddress.clear();
    }
}

} // namespace NetCore
