#pragma once

#include <QHash>
#include <vector>
#include <QRegion>
#include <QPointer>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QTimer>

#include "rdp/RemoteSession.h"

// Shows a RemoteSession's desktop (scaled to fit, aspect kept) and forwards mouse
// and keyboard input to it. The picture is a GPU texture scaled by the scene graph, so a large
// (Retina) remote screen costs an upload per frame, not a CPU resample.
class RdpView : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(RemoteSession *session READ session WRITE setSession NOTIFY sessionChanged)
    // Index into session.monitors to show only that display's part (-1 = the whole desktop).
    Q_PROPERTY(int monitor READ monitor WRITE setMonitor NOTIFY monitorChanged)
public:
    explicit RdpView(QQuickItem *parent = nullptr);
    ~RdpView() override;

    RemoteSession *session() const { return m_session; }
    void setSession(RemoteSession *s);
    int monitor() const { return m_monitor; }
    void setMonitor(int m);


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
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    QRect sourceRect() const;   // the part of the desktop this view shows
    QRectF targetRect() const;
    QPoint toRemote(const QPointF &p) const;
    // While a button is held the mouse stays grabbed by this view, so a drag past its edge is mapped
    // through the view of the same session under the cursor (another remote monitor's window).
    QPoint dragToRemote(const QMouseEvent *e) const;
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
    bool m_textureDirty = true;   // m_frame changed since the last upload (render thread reads it during sync)
    // The picture is uploaded as tiles, and only changed tiles go to the GPU again.
    static constexpr int kTile = 512;
    std::vector<char> m_tileDirty;   // one per tile of m_frame, row by row
    int m_tileCols = 0, m_tileRows = 0;
    void markTiles(const QRect &frameRect);   // in m_frame coordinates
    // Frames shown and upload cost, logged every 5 s while the picture changes.
    QElapsedTimer m_statClock;
    int m_statFrames = 0, m_statUpdates = 0, m_statRefreshes = 0, m_statTiles = 0;
    qint64 m_statUploadNs = 0;
};
