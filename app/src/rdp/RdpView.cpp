#include "rdp/RdpView.h"

#include <QKeyEvent>
#include <QPainter>

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
}

void RdpView::setSession(RdpSession *s)
{
    if (m_session == s)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = s;
    if (s) {
        connect(s, &RdpSession::frameUpdated, this, [this](const QRect &) {
            m_frame = m_session->frame();
            update();
        });
        connect(s, &RdpSession::desktopSizeChanged, this, [this] {
            m_frame = m_session->frame();
            update();
        });
    }
    emit sessionChanged();
}

QRectF RdpView::targetRect() const
{
    if (m_frame.isNull())
        return {};
    QSizeF size = QSizeF(m_frame.size()).scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
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
    if (r.isEmpty())
        return {};
    const double sx = m_frame.width() / r.width(), sy = m_frame.height() / r.height();
    return QPoint(qBound(0, int((p.x() - r.left()) * sx), m_frame.width() - 1),
                  qBound(0, int((p.y() - r.top()) * sy), m_frame.height() - 1));
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
