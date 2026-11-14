#include "rdp/FreeRdpEngine.h"

#ifdef BURROW_HAVE_FREERDP
#include <freerdp/version.h>
#include <freerdp/freerdp.h>
#endif

QString FreeRdpEngine::engineVersion()
{
#ifdef BURROW_HAVE_FREERDP
    return QStringLiteral("FreeRDP %1").arg(QString::fromUtf8(freerdp_get_version_string()));
#else
    return QStringLiteral("FreeRDP yok");
#endif
}

bool FreeRdpEngine::connectTo(const RdpConnectParams &params)
{
    Q_UNUSED(params);
    emit disconnected(tr("RDP engine not implemented yet"));
    return false;
}

void FreeRdpEngine::disconnectFrom()
{
}
