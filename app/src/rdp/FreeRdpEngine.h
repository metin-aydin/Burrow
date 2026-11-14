#pragma once

#include "rdp/RdpEngine.h"

// FreeRDP 3 backed engine. Skeleton only: session setup, GDI/GFX rendering,
// multimon and clipboard are implemented in later milestones.
class FreeRdpEngine : public RdpEngine
{
    Q_OBJECT
public:
    using RdpEngine::RdpEngine;

    static QString engineVersion();

    bool connectTo(const RdpConnectParams &params) override;
    void disconnectFrom() override;
};
