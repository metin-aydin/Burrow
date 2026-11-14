#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

// Engine-agnostic RDP interface (ADR 0001): FreeRDP today, IronRDP possibly
// later. The UI only talks to this interface.
struct RdpMonitor
{
    QRect geometry;     // in remote desktop coordinates
    bool primary = false;
    int scalePercent = 100;
};

struct RdpConnectParams
{
    QString host;       // usually 127.0.0.1 (netcore loopback forward)
    quint16 port = 3389;
    QString username;
    QString domain;
    QString password;   // never logged
    QList<RdpMonitor> monitors;
    bool clipboard = true;
};

class RdpEngine : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~RdpEngine() override = default;

    virtual bool connectTo(const RdpConnectParams &params) = 0;
    virtual void disconnectFrom() = 0;

signals:
    void connected();
    void disconnected(const QString &reason);
};
