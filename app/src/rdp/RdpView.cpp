#include "rdp/RdpView.h"

#include <QKeyEvent>
#include <QPainter>
#include <QQuickWindow>

#include <winpr/input.h>

RdpView::RdpView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, false);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(false);
    setFillColor(Qt::black);
    setOpaquePainting(true);
    m_frameTimer.setSingleShot(true);
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, &RdpView::refreshFrame);
    // Follow the window size once it settles (dragging sends many intermediate sizes).
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(400);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this] {
        if (!m_session || !window() || width() < 50 || height() < 50)
            return;
        // Logical pixels unless the device uses Retina resolution (then scale() is 200 on Retina).
        const qreal dpr = m_session->scale() / 100.0;
        m_session->requestResize(QSize(qRound(width() * dpr), qRound(height() * dpr)), m_session->scale());
    });
}

void RdpView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        m_resizeTimer.start();
}

void RdpView::setSession(RdpSession *s)
{
    if (m_session == s)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = s;
    if (s) {
        connect(s, &RdpSession::frameUpdated, this, [this](const QRect &r) {
            // Only updates that touch the part this view shows; at most one copy per frame interval.
            if (!m_frame.isNull() && !r.intersects(sourceRect()))
                return;
            if (!m_frameTimer.isActive())
                m_frameTimer.start();
        });
        connect(s, &RdpSession::desktopSizeChanged, this, &RdpView::refreshFrame);
        // Show the current picture right away: a static remote screen may not send updates for a while.
        refreshFrame();
        connect(s, &RdpSession::stateChanged, this, [this] {
            if (m_session && m_session->state() == QLatin1String("connected"))
                m_resizeTimer.start();   // match the window as soon as the display channel is up
        });
    }
    emit sessionChanged();
}

void RdpView::setMonitor(int m)
{
    if (m_monitor == m)
        return;
    m_monitor = m;
    refreshFrame();
    emit monitorChanged();
}

void RdpView::refreshFrame()
{
    if (!m_session)
        return;
    // Only this view's part of the desktop: with several monitors the full picture is large.
    m_frame = m_session->frame(sourceRect());
    update();
}

QRect RdpView::sourceRect() const
{
    const QRect all(QPoint(0, 0), m_session ? m_session->desktopSize() : QSize());
    if (m_monitor < 0 || !m_session || !m_session->multiMonitor())
        return all;
    const QVariantList mons = m_session->monitors();
    if (m_monitor >= mons.size())
        return all;
    const QVariantMap m = mons.at(m_monitor).toMap();
    return QRect(m.value("x").toInt(), m.value("y").toInt(), m.value("width").toInt(), m.value("height").toInt())
        .intersected(all);
}

QRectF RdpView::targetRect() const
{
    if (m_frame.isNull())
        return {};
    QSizeF size = QSizeF(sourceRect().size()).scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

void RdpView::paint(QPainter *painter)
{
    if (m_frame.isNull())
        return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(targetRect(), m_frame);
}

QPoint RdpView::toRemote(const QPointF &p) const
{
    const QRectF r = targetRect();
    const QRect src = sourceRect();
    if (r.isEmpty() || src.isEmpty())
        return {};
    const double sx = src.width() / r.width(), sy = src.height() / r.height();
    return QPoint(src.left() + qBound(0, int((p.x() - r.left()) * sx), src.width() - 1),
                  src.top() + qBound(0, int((p.y() - r.top()) * sy), src.height() - 1));
}

void RdpView::mousePressEvent(QMouseEvent *e)
{
    forceActiveFocus();
    if (m_session) {
        const QPoint p = toRemote(e->position());
        m_session->sendMouseButton(e->button(), true, p.x(), p.y());
    }
    e->accept();
}

void RdpView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_session) {
        const QPoint p = toRemote(e->position());
        m_session->sendMouseButton(e->button(), false, p.x(), p.y());
    }
    e->accept();
}

void RdpView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_session) {
        const QPoint p = toRemote(e->position());
        m_session->sendMouseMove(p.x(), p.y());
    }
}

void RdpView::hoverMoveEvent(QHoverEvent *e)
{
    if (m_session) {
        const QPoint p = toRemote(e->position());
        m_session->sendMouseMove(p.x(), p.y());
    }
}

void RdpView::wheelEvent(QWheelEvent *e)
{
    if (!m_session)
        return;
    // Mice report 120 per notch; trackpads report small steps, so accumulate.
    m_wheelAccum += e->angleDelta();
    const QPoint p = toRemote(e->position());
    const int v = (m_wheelAccum.y() / 30) * 30, h = (m_wheelAccum.x() / 30) * 30;
    if (v != 0) {
        m_session->sendWheel(v, false, p.x(), p.y());
        m_wheelAccum.ry() -= v;
    }
    if (h != 0) {
        m_session->sendWheel(-h, true, p.x(), p.y());
        m_wheelAccum.rx() -= h;
    }
    e->accept();
}

unsigned RdpView::scancodeForMacKey(quint32 macKeyCode)
{
    const DWORD vk = GetVirtualKeyCodeFromKeycode(macKeyCode, WINPR_KEYCODE_TYPE_APPLE);
    return vk ? GetVirtualScanCodeFromVirtualKeyCode(vk, WINPR_KBD_TYPE_IBM_ENHANCED) : 0;
}

unsigned RdpView::scancodeFor(const QKeyEvent *e) const
{
#ifdef Q_OS_MACOS
    return scancodeForMacKey(e->nativeVirtualKey());
#else
    // XKB keycode (Linux) / scancode (Windows) mapping follows with those ports.
    const DWORD vk = GetVirtualKeyCodeFromKeycode(e->nativeScanCode(), WINPR_KEYCODE_TYPE_XKB);
    return vk ? GetVirtualScanCodeFromVirtualKeyCode(vk, WINPR_KBD_TYPE_IBM_ENHANCED) : 0;
#endif
}

void RdpView::keyPressEvent(QKeyEvent *e)
{
    // Auto-repeat is sent as repeated key-downs, like a real keyboard.
    if (m_session)
        if (const unsigned sc = scancodeFor(e))
            m_session->sendKey(sc, true);
    e->accept();
}

void RdpView::keyReleaseEvent(QKeyEvent *e)
{
    if (m_session && !e->isAutoRepeat()) {
        if (const unsigned sc = scancodeFor(e))
            m_session->sendKey(sc, false);
    }
    e->accept();
}

void RdpView::focusOutEvent(QFocusEvent *e)
{
    // Don't leave modifiers stuck on the remote side when switching apps.
    if (m_session)
        m_session->releaseKeys();
    QQuickPaintedItem::focusOutEvent(e);
}
