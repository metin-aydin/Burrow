#include "net/NetCore.h"

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
