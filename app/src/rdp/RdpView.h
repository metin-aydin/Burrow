#pragma once

#include <QHash>
#include <QRegion>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>

#include "rdp/RemoteSession.h"

// Shows a RemoteSession's desktop (scaled to fit, aspect kept) and forwards mouse
// and keyboard input to it.
class RdpView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(RemoteSession *session READ session WRITE setSession NOTIFY sessionChanged)
    // Index into session.monitors to show only that display's part (-1 = the whole desktop).
    Q_PROPERTY(int monitor READ monitor WRITE setMonitor NOTIFY monitorChanged)
public:
    explicit RdpView(QQuickItem *parent = nullptr);

    RemoteSession *session() const { return m_session; }
    void setSession(RemoteSession *s);
    int monitor() const { return m_monitor; }
    void setMonitor(int m);

    void paint(QPainter *painter) override;

    // Mac virtual key code (QKeyEvent::nativeVirtualKey) → RDP scancode.
    static unsigned scancodeForMacKey(quint32 macKeyCode);

signals:
    void sessionChanged();
    void monitorChanged();

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
    QRect sourceRect() const;   // the part of the desktop this view shows
    QRectF targetRect() const;
    QPoint toRemote(const QPointF &p) const;
    unsigned scancodeFor(const QKeyEvent *e) const;
    static quint32 keysymFor(const QKeyEvent *e);   // X keysym for VNC sessions
    QHash<quint32, quint32> m_keysymsDown;          // native key -> keysym sent on press

    QPointer<RemoteSession> m_session;
    QImage m_frame;
    QPoint m_wheelAccum;
    QTimer m_resizeTimer;
    QTimer m_frameTimer;     // coalesces desktop updates (one copy per ~16 ms, not per update)
    void refreshFrame();     // whole picture (size, monitor or session changed)
    void refreshDirty();     // only what changed since the last refresh
    QRegion m_dirty;         // desktop coordinates, inside sourceRect()
    int m_monitor = -1;
};
