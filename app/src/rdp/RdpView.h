#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>

#include "rdp/RdpSession.h"

// Shows an RdpSession's desktop (scaled to fit, aspect kept) and forwards mouse
// and keyboard input to it.
class RdpView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(RdpSession *session READ session WRITE setSession NOTIFY sessionChanged)
public:
    explicit RdpView(QQuickItem *parent = nullptr);

    RdpSession *session() const { return m_session; }
    void setSession(RdpSession *s);

    void paint(QPainter *painter) override;

    // Mac virtual key code (QKeyEvent::nativeVirtualKey) → RDP scancode.
    static unsigned scancodeForMacKey(quint32 macKeyCode);

signals:
    void sessionChanged();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QRectF targetRect() const;
    QPoint toRemote(const QPointF &p) const;
    unsigned scancodeFor(const QKeyEvent *e) const;

    QPointer<RdpSession> m_session;
    QImage m_frame;
    QPoint m_wheelAccum;
    QTimer m_resizeTimer;
};
