#include "rdp/RdpSession.h"

#include <QCoreApplication>
#include <QRect>

#include <freerdp/channels/channels.h>
#include <freerdp/channels/cliprdr.h>
#include <freerdp/channels/disp.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/client.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/error.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <winpr/synch.h>

namespace {

struct BurrowContext
{
    rdpClientContext common; // must be first
    RdpSession *session;
};

RdpSession *sessionOf(rdpContext *ctx) { return reinterpret_cast<BurrowContext *>(ctx)->session; }

BOOL endPaint(rdpContext *ctx)
{
    rdpGdi *gdi = ctx->gdi;
    if (!gdi || !gdi->primary || !gdi->primary->hdc || !gdi->primary->hdc->hwnd)
        return TRUE;
    HGDI_RGN invalid = gdi->primary->hdc->hwnd->invalid;
    if (invalid->null)
        return TRUE;
    sessionOf(ctx)->onPaint(gdi, invalid->x, invalid->y, invalid->w, invalid->h);
    return TRUE;
}

BOOL desktopResize(rdpContext *ctx)
{
    rdpSettings *s = ctx->settings;
    if (!gdi_resize(ctx->gdi, freerdp_settings_get_uint32(s, FreeRDP_DesktopWidth),
                    freerdp_settings_get_uint32(s, FreeRDP_DesktopHeight)))
        return FALSE;
    sessionOf(ctx)->onResize(ctx->gdi);
    return TRUE;
}

void onChannelConnected(void *context, const ChannelConnectedEventArgs *e)
{
    sessionOf(static_cast<rdpContext *>(context))->onChannelConnected(e->name, e->pInterface);
}

void onChannelDisconnected(void *context, const ChannelDisconnectedEventArgs *e)
{
    sessionOf(static_cast<rdpContext *>(context))->onChannelDisconnected(e->name);
}

BOOL preConnect(freerdp *instance)
{
    rdpSettings *s = instance->context->settings;
    freerdp_settings_set_uint32(s, FreeRDP_OsMajorType, OSMAJORTYPE_MACINTOSH);
    freerdp_settings_set_uint32(s, FreeRDP_OsMinorType, OSMINORTYPE_MACINTOSH);
    PubSub_SubscribeChannelConnected(instance->context->pubSub, freerdp_client_OnChannelConnectedEventHandler);
    PubSub_SubscribeChannelDisconnected(instance->context->pubSub, freerdp_client_OnChannelDisconnectedEventHandler);
    PubSub_SubscribeChannelConnected(instance->context->pubSub, onChannelConnected);
    PubSub_SubscribeChannelDisconnected(instance->context->pubSub, onChannelDisconnected);
    return freerdp_client_load_addins(instance->context->channels, s);
}

BOOL postConnect(freerdp *instance)
{
    if (!gdi_init(instance, PIXEL_FORMAT_BGRX32))
        return FALSE;
    instance->context->update->EndPaint = endPaint;
    instance->context->update->DesktopResize = desktopResize;
    sessionOf(instance->context)->onResize(instance->context->gdi);
    return TRUE;
}

void postDisconnect(freerdp *instance)
{
    PubSub_UnsubscribeChannelConnected(instance->context->pubSub, freerdp_client_OnChannelConnectedEventHandler);
    PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub, freerdp_client_OnChannelDisconnectedEventHandler);
    PubSub_UnsubscribeChannelConnected(instance->context->pubSub, onChannelConnected);
    PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub, onChannelDisconnected);
    gdi_free(instance);
}

// Credentials are set before connecting; FreeRDP asks only if they are missing.
BOOL authenticate(freerdp *, char **username, char **password, char **, rdp_auth_reason)
{
    return username && *username && password && *password;
}

DWORD verifyCertificate(freerdp *instance, const char *, UINT16, const char *, const char *, const char *,
                        const char *fingerprint, DWORD)
{
    return sessionOf(instance->context)->onVerifyCertificate(QString::fromUtf8(fingerprint));
}

DWORD verifyChangedCertificate(freerdp *instance, const char *, UINT16, const char *, const char *,
                               const char *, const char *newFingerprint, const char *, const char *,
                               const char *, DWORD)
{
    return sessionOf(instance->context)->onVerifyCertificate(QString::fromUtf8(newFingerprint));
}

BOOL clientNew(freerdp *instance, rdpContext *)
{
    instance->PreConnect = preConnect;
    instance->PostConnect = postConnect;
    instance->PostDisconnect = postDisconnect;
    instance->AuthenticateEx = authenticate;
    instance->VerifyCertificateEx = verifyCertificate;
    instance->VerifyChangedCertificateEx = verifyChangedCertificate;
    return TRUE;
}

QString friendlyError(UINT32 code)
{
    switch (code) {
    case FREERDP_ERROR_AUTHENTICATION_FAILED:
    case FREERDP_ERROR_CONNECT_LOGON_FAILURE:
    case FREERDP_ERROR_CONNECT_WRONG_PASSWORD:
        return QCoreApplication::translate("RdpSession", "The computer rejected the username or password.");
    case FREERDP_ERROR_CONNECT_ACCOUNT_LOCKED_OUT:
        return QCoreApplication::translate("RdpSession", "The account is locked on the remote computer.");
    case FREERDP_ERROR_DNS_NAME_NOT_FOUND:
        return QCoreApplication::translate("RdpSession", "The computer's address couldn't be found.");
    case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
    case FREERDP_ERROR_CONNECT_FAILED:
        return QCoreApplication::translate("RdpSession", "Couldn't reach the computer. Check the address and the tunnel.");
    case FREERDP_ERROR_TLS_CONNECT_FAILED:
        return QCoreApplication::translate("RdpSession", "The secure connection couldn't be established.");
    case FREERDP_ERROR_CONNECT_CANCELLED:
        return QCoreApplication::translate("RdpSession", "The computer's certificate doesn't match the one you trusted, so Burrow didn't connect. If the change is expected, use Forget Saved Certificate in the device settings.");
    default:
        return QCoreApplication::translate("RdpSession", "The connection failed (%1).")
            .arg(QString::fromUtf8(freerdp_get_last_error_string(code)));
    }
}

} // namespace

RdpSession::RdpSession(Params params, QObject *parent)
    : QObject(parent)
    , m_p(std::move(params))
    , m_keys(KeyMapper::modeFromString(m_p.keyMap))
{
    m_clip = new RdpClipboard(RdpClipboard::modeFromString(m_p.clipboard),
                              [this](std::function<void()> f) { post(std::move(f)); }, this);
}

RdpSession::~RdpSession()
{
    m_stop = true;
    m_thread.quit();
    m_thread.wait();
    // Wipe the password copy.
    m_p.password.fill(QChar(0));
}

QSize RdpSession::desktopSize() const
{
    QMutexLocker lock(&m_frameLock);
    return m_frame.size();
}

QImage RdpSession::frame() const
{
    QMutexLocker lock(&m_frameLock);
    return m_frame.copy();
}

void RdpSession::start()
{
    connect(&m_thread, &QThread::started, this, [this] { run(); }, Qt::DirectConnection);
    m_thread.start();
}

void RdpSession::close()
{
    if (m_state == QLatin1String("closed"))
        return;
    if (m_p.lockOnDisconnect && m_state == QLatin1String("connected")) {
        // Win/Super+L locks Windows and GNOME before the session is released.
        releaseKeys();
        sendScancode(KeyMapper::LWin, true);
        sendScancode(0x26, true);
        sendScancode(0x26, false);
        sendScancode(KeyMapper::LWin, false);
        QThread::msleep(300);
    }
    m_stop = true;
    answerCertificate(false);   // unblock a pending certificate prompt
}

void RdpSession::setState(const QString &state, const QString &error)
{
    QMetaObject::invokeMethod(this, [this, state, error] {
        m_state = state;
        m_error = error;
        emit stateChanged();
        if (state == QLatin1String("closed"))
            emit closed(this);
    }, Qt::QueuedConnection);
}

void RdpSession::run()
{
    RDP_CLIENT_ENTRY_POINTS ep{};
    ep.Size = sizeof(ep);
    ep.Version = RDP_CLIENT_INTERFACE_VERSION;
    ep.ContextSize = sizeof(BurrowContext);
    ep.ClientNew = clientNew;
    rdpContext *ctx = freerdp_client_context_new(&ep);
    if (!ctx) {
        setState(QStringLiteral("closed"), tr("Couldn't start the RDP engine."));
        return;
    }
    reinterpret_cast<BurrowContext *>(ctx)->session = this;
    freerdp *instance = ctx->instance;
    rdpSettings *s = ctx->settings;

    const QByteArray host = m_p.host.toUtf8(), user = m_p.username.toUtf8(), domain = m_p.domain.toUtf8();
    QByteArray pass = m_p.password.toUtf8();
    freerdp_settings_set_string(s, FreeRDP_ServerHostname, host.constData());
    freerdp_settings_set_uint32(s, FreeRDP_ServerPort, m_p.port);
    if (!user.isEmpty())
        freerdp_settings_set_string(s, FreeRDP_Username, user.constData());
    if (!pass.isEmpty())
        freerdp_settings_set_string(s, FreeRDP_Password, pass.constData());
    if (!domain.isEmpty())
        freerdp_settings_set_string(s, FreeRDP_Domain, domain.constData());
    std::fill(pass.begin(), pass.end(), '\0');
    freerdp_settings_set_uint32(s, FreeRDP_DesktopWidth, uint32_t(m_p.size.width()));
    freerdp_settings_set_uint32(s, FreeRDP_DesktopHeight, uint32_t(m_p.size.height()));
    freerdp_settings_set_uint32(s, FreeRDP_ColorDepth, 32);
    freerdp_settings_set_bool(s, FreeRDP_SupportGraphicsPipeline, TRUE);  // GNOME needs GFX
    freerdp_settings_set_bool(s, FreeRDP_NetworkAutoDetect, TRUE);
    freerdp_settings_set_uint32(s, FreeRDP_ConnectionType, CONNECTION_TYPE_AUTODETECT);
    freerdp_settings_set_bool(s, FreeRDP_AutoReconnectionEnabled, TRUE);
    freerdp_settings_set_bool(s, FreeRDP_DesktopResize, TRUE);
    freerdp_settings_set_uint32(s, FreeRDP_KeyboardType, 4);
    // Resize with the window (display control channel) and keep text crisp on Retina screens.
    freerdp_settings_set_bool(s, FreeRDP_SupportDisplayControl, TRUE);
    freerdp_settings_set_bool(s, FreeRDP_DynamicResolutionUpdate, TRUE);
    freerdp_settings_set_uint32(s, FreeRDP_DesktopScaleFactor, uint32_t(m_p.scale));
    freerdp_settings_set_uint32(s, FreeRDP_DeviceScaleFactor, m_p.scale >= 200 ? 180 : m_p.scale >= 140 ? 140 : 100);
    freerdp_settings_set_bool(s, FreeRDP_RedirectClipboard, m_clip->enabled() ? TRUE : FALSE);
    // Only NLA/TLS: legacy "RDP security" has no server authentication, so a man in the middle could
    // bypass the certificate pin. Servers that offer only that are refused.
    freerdp_settings_set_bool(s, FreeRDP_RdpSecurity, FALSE);
    freerdp_settings_set_bool(s, FreeRDP_UseRdpSecurityLayer, FALSE);
    freerdp_settings_set_bool(s, FreeRDP_NlaSecurity, TRUE);
    freerdp_settings_set_bool(s, FreeRDP_TlsSecurity, TRUE);
    freerdp_settings_set_bool(s, FreeRDP_NegotiateSecurityLayer, TRUE);

    if (!freerdp_connect(instance)) {
        const UINT32 code = freerdp_get_last_error(ctx);
        setState(QStringLiteral("closed"),
                 m_certChanged ? friendlyError(FREERDP_ERROR_CONNECT_CANCELLED)
                 : m_stop ? QString() : friendlyError(code));
        freerdp_client_context_free(ctx);
        return;
    }
    m_instance = instance;
    setState(QStringLiteral("connected"));

    while (!m_stop && !freerdp_shall_disconnect_context(ctx)) {
        HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
        const DWORD n = freerdp_get_event_handles(ctx, handles, ARRAYSIZE(handles));
        if (n == 0)
            break;
        if (WaitForMultipleObjects(n, handles, FALSE, 100) == WAIT_FAILED)
            break;
        drainTasks();
        if (!freerdp_check_event_handles(ctx)) {
            if (freerdp_get_last_error(ctx) == FREERDP_ERROR_SUCCESS)
                continue;
            break;
        }
    }
    const UINT32 code = freerdp_get_last_error(ctx);
    const bool userClosed = m_stop;
    m_instance = nullptr;
    freerdp_disconnect(instance);
    freerdp_client_context_free(ctx);
    setState(QStringLiteral("closed"),
             userClosed || code == FREERDP_ERROR_SUCCESS ? QString() : friendlyError(code));
}

void RdpSession::onResize(rdpGdi *gdi)
{
    {
        QMutexLocker lock(&m_frameLock);
        m_frame = QImage(int(gdi->width), int(gdi->height), QImage::Format_RGB32);
        m_frame.fill(Qt::black);
    }
    QMetaObject::invokeMethod(this, &RdpSession::desktopSizeChanged, Qt::QueuedConnection);
}

void RdpSession::onPaint(rdpGdi *gdi, int x, int y, int w, int h)
{
    const QRect r = QRect(x, y, w, h).intersected(QRect(0, 0, int(gdi->width), int(gdi->height)));
    if (r.isEmpty())
        return;
    {
        QMutexLocker lock(&m_frameLock);
        if (m_frame.size() != QSize(int(gdi->width), int(gdi->height)))
            return;
        // BGRX32 matches QImage::Format_RGB32 byte order on little-endian machines.
        for (int row = r.top(); row <= r.bottom(); ++row) {
            const uchar *src = gdi->primary_buffer + size_t(row) * gdi->stride + size_t(r.left()) * 4;
            memcpy(m_frame.scanLine(row) + r.left() * 4, src, size_t(r.width()) * 4);
        }
    }
    emit frameUpdated(r);
}

QString RdpSession::pinFor(const QString &fingerprint) const
{
    const QString host = m_p.pinHost.isEmpty() ? m_p.host : m_p.pinHost;
    const quint16 port = m_p.pinPort ? m_p.pinPort : m_p.port;
    return QStringLiteral("%1:%2|%3").arg(host.trimmed().toLower()).arg(port).arg(fingerprint.trimmed().toLower());
}

unsigned RdpSession::onVerifyCertificate(const QString &fingerprint)
{
    const QString pin = pinFor(fingerprint);
    if (!m_p.knownFingerprint.isEmpty()) {
        // Fail closed: once a device has a pin, only that exact certificate is accepted. Replacing it
        // needs an explicit "Forget Saved Certificate" (or changing the address) in the device editor.
        const QString known = m_p.knownFingerprint.trimmed().toLower();
        const bool match = known.contains(QLatin1Char('|'))
            ? known == pin
            : known == fingerprint.trimmed().toLower();   // legacy bare fingerprint
        if (match)
            return 2;
        m_certChanged = true;
        return 0;
    }
    // No pin yet: ask the user and wait (up to 2 minutes).
    QMutexLocker lock(&m_certLock);
    m_certAnswer = -1;
    QMetaObject::invokeMethod(this, [this, fingerprint] {
        emit certificatePrompt(QStringLiteral("%1:%2").arg(m_p.pinHost.isEmpty() ? m_p.host : m_p.pinHost)
                                   .arg(m_p.pinPort ? m_p.pinPort : m_p.port), fingerprint);
    }, Qt::QueuedConnection);
    m_certCond.wait(&m_certLock, QDeadlineTimer(120000));
    if (m_certAnswer != 1 || m_stop)
        return 0;
    emit certificateTrusted(m_p.deviceId, pin);
    return 2; // accept for this session; Burrow keeps the pin itself
}

void RdpSession::answerCertificate(bool trust)
{
    QMutexLocker lock(&m_certLock);
    m_certAnswer = trust ? 1 : 0;
    m_certCond.wakeAll();
}

void RdpSession::sendScancode(unsigned scancode, bool down)
{
    freerdp *inst = m_instance;
    if (!inst)
        return;
    freerdp_input_send_keyboard_event_ex(inst->context->input, down ? TRUE : FALSE, FALSE, scancode);
}

void RdpSession::sendKey(unsigned scancode, bool down)
{
    for (const KeyMapper::Event &e : m_keys.map(scancode, down))
        sendScancode(e.scancode, e.down);
}

void RdpSession::releaseKeys()
{
    for (const KeyMapper::Event &e : m_keys.releaseAll())
        sendScancode(e.scancode, e.down);
}

void RdpSession::sendMouseMove(int x, int y)
{
    freerdp *inst = m_instance;
    if (!inst)
        return;
    freerdp_input_send_mouse_event(inst->context->input, PTR_FLAGS_MOVE, UINT16(qMax(0, x)), UINT16(qMax(0, y)));
}

void RdpSession::sendMouseButton(Qt::MouseButton button, bool down, int x, int y)
{
    freerdp *inst = m_instance;
    if (!inst)
        return;
    UINT16 flags = 0;
    switch (button) {
    case Qt::LeftButton: flags = PTR_FLAGS_BUTTON1; break;
    case Qt::RightButton: flags = PTR_FLAGS_BUTTON2; break;
    case Qt::MiddleButton: flags = PTR_FLAGS_BUTTON3; break;
    default: return;
    }
    if (down)
        flags |= PTR_FLAGS_DOWN;
    freerdp_input_send_mouse_event(inst->context->input, flags, UINT16(qMax(0, x)), UINT16(qMax(0, y)));
}

void RdpSession::sendWheel(int delta, bool horizontal, int x, int y)
{
    freerdp *inst = m_instance;
    if (!inst || delta == 0)
        return;
    const int step = qBound(-255, delta, 255);
    UINT16 flags = horizontal ? PTR_FLAGS_HWHEEL : PTR_FLAGS_WHEEL;
    if (step < 0)
        flags |= PTR_FLAGS_WHEEL_NEGATIVE | UINT16((0x100 + step) & 0xFF);
    else
        flags |= UINT16(step & 0xFF);
    freerdp_input_send_mouse_event(inst->context->input, flags, UINT16(qMax(0, x)), UINT16(qMax(0, y)));
}

void RdpSession::post(std::function<void()> task)
{
    QMutexLocker lock(&m_tasksLock);
    m_tasks.append(std::move(task));
}

void RdpSession::drainTasks()
{
    QList<std::function<void()>> tasks;
    {
        QMutexLocker lock(&m_tasksLock);
        tasks.swap(m_tasks);
    }
    for (auto &t : tasks)
        t();
}

void RdpSession::onChannelConnected(const char *name, void *iface)
{
    if (strcmp(name, CLIPRDR_SVC_CHANNEL_NAME) == 0 && m_clip->enabled())
        m_clip->attach(static_cast<CliprdrClientContext *>(iface));
    else if (strcmp(name, DISP_DVC_CHANNEL_NAME) == 0)
        m_disp = iface;
}

void RdpSession::onChannelDisconnected(const char *name)
{
    if (strcmp(name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
        m_clip->detach();
    else if (strcmp(name, DISP_DVC_CHANNEL_NAME) == 0)
        m_disp = nullptr;
}

void RdpSession::requestResize(const QSize &pixels, int scale)
{
    // The protocol wants even widths between 200 and 8192 pixels.
    const int w = qBound(200, pixels.width() & ~1, 8192);
    const int h = qBound(200, pixels.height(), 8192);
    post([this, w, h, scale] {
        auto *disp = static_cast<DispClientContext *>(m_disp);
        if (!disp || desktopSize() == QSize(w, h))
            return;
        DISPLAY_CONTROL_MONITOR_LAYOUT layout{};
        layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
        layout.Width = UINT32(w);
        layout.Height = UINT32(h);
        layout.DesktopScaleFactor = UINT32(scale);
        layout.DeviceScaleFactor = scale >= 200 ? 180 : scale >= 140 ? 140 : 100;
        layout.PhysicalWidth = UINT32(w * 254 / 960);   // assume 96 dpi at 100 %
        layout.PhysicalHeight = UINT32(h * 254 / 960);
        (void)disp->SendMonitorLayout(disp, 1, &layout);
    });
}
