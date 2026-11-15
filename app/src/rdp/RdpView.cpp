#include "rdp/RdpView.h"

#include <QCursor>
#include <QPixmap>

#include <QKeyEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>

#include <winpr/input.h>

RdpView::RdpView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, false);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(false);
    m_frameTimer.setSingleShot(true);
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, &RdpView::refreshDirty);
    // Follow the window size once it settles (dragging sends many intermediate sizes).
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(400);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this] {
        if (!m_session || !window() || width() < 50 || height() < 50)
            return;
        // Logical pixels unless the device uses Retina resolution (then scale() is 200 on Retina).
        const qreal dpr = m_session->scale() / 100.0;
        const QSize px(qRound(width() * dpr), qRound(height() * dpr));
        // A view of one monitor of several resizes that monitor; otherwise the whole desktop follows.
        if (m_monitor >= 0 && m_session->multiMonitor())
            m_session->requestMonitorSize(m_monitor, px);
        else
            m_session->requestResize(px, m_session->scale());
    });
}

void RdpView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();   // the picture's place follows the item's size
    if (newGeometry.size() != oldGeometry.size())
        m_resizeTimer.start();
}

void RdpView::setSession(RemoteSession *s)
{
    if (m_session == s)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = s;
    if (s) {
        connect(s, &RemoteSession::frameUpdated, this, [this](const QRect &r) {
            // Only updates that touch the part this view shows, gathered and copied at most once a frame.
            const QRect part = r.intersected(sourceRect());
            if (part.isEmpty() && !m_frame.isNull())
                return;
            m_dirty += part;
            ++m_statUpdates;
            if (!m_frameTimer.isActive())
                m_frameTimer.start();
        });
        connect(s, &RemoteSession::desktopSizeChanged, this, [this] {
            refreshFrame();
            if (m_monitor >= 0)
                m_resizeTimer.start();   // monitors became known (or changed): fit ours to this view
        });
        // The remote pointer's shape, where the protocol sends one (a Mac's I-beam, hand, resize arrows).
        connect(s, &RemoteSession::cursorChanged, this, [this] {
            const QImage img = m_session ? m_session->cursorImage() : QImage();
            if (img.isNull())
                unsetCursor();
            else
                setCursor(QCursor(QPixmap::fromImage(img), m_session->cursorHotspot().x(), m_session->cursorHotspot().y()));
        });
        // Show the current picture right away: a static remote screen may not send updates for a while.
        refreshFrame();
        connect(s, &RemoteSession::stateChanged, this, [this] {
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
    m_resizeTimer.start();   // the monitor now shown takes this view's size
    emit monitorChanged();
}

void RdpView::refreshFrame()
{
    if (!m_session)
        return;
    // Only this view's part of the desktop: with several monitors the full picture is large.
    m_frame = m_session->frame(sourceRect());
    m_dirty = QRegion();
    m_textureDirty = true;
    markTiles(m_frame.rect());
    update();
}

void RdpView::markTiles(const QRect &r)
{
    const int cols = (m_frame.width() + kTile - 1) / kTile, rows = (m_frame.height() + kTile - 1) / kTile;
    if (cols != m_tileCols || rows != m_tileRows) {
        m_tileCols = cols;
        m_tileRows = rows;
        m_tileDirty.assign(size_t(cols) * size_t(rows), 1);
        return;
    }
    const QRect in = r.intersected(m_frame.rect());
    if (in.isEmpty())
        return;
    for (int ty = in.top() / kTile; ty <= in.bottom() / kTile; ++ty)
        for (int tx = in.left() / kTile; tx <= in.right() / kTile; ++tx)
            m_tileDirty[size_t(ty) * size_t(m_tileCols) + size_t(tx)] = 1;
}

void RdpView::refreshDirty()
{
    const QRect src = sourceRect();
    if (!m_session || m_frame.isNull() || m_frame.size() != src.size()) {
        refreshFrame();
        return;
    }
    const QRegion dirty = m_dirty.intersected(src);
    m_dirty = QRegion();
    if (dirty.isEmpty())
        return;
    ++m_statRefreshes;
    // Copy the changed rectangles into our picture; the next frame uploads it.
    for (const QRect &r : dirty) {
        m_session->copyInto(m_frame, r, r.topLeft() - src.topLeft());
        markTiles(r.translated(-src.topLeft()));
    }
    m_textureDirty = true;
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
    // A remote monitor slightly off the view's shape (the server can't be resized right now, see
    // RdpSession::m_handedOver) fills the view rather than leaving black bars; a larger difference
    // keeps its proportions.
    const QSizeF src(sourceRect().size()), view(width(), height());
    if (src.isEmpty() || view.isEmpty())
        return {};
    const double ratio = (view.width() / view.height()) / (src.width() / src.height());
    if (ratio > 0.92 && ratio < 1.087)
        return QRectF(QPointF(0, 0), view);
    QSizeF size = src.scaled(view, Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

// Render thread, while the GUI thread waits: a black background, then the picture as a grid of
// textures, of which only changed tiles are uploaded again.
QSGNode *RdpView::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old;
    if (!root) {
        root = new QSGNode;
        QSGRectangleNode *bg = window()->createRectangleNode();
        bg->setColor(Qt::black);
        root->appendChildNode(bg);
        root->appendChildNode(new QSGNode);   // the tiles
        markTiles(m_frame.rect());
    }
    static_cast<QSGRectangleNode *>(root->childAtIndex(0))->setRect(boundingRect());
    QSGNode *grid = root->childAtIndex(1);
    const int want = m_frame.isNull() ? 0 : m_tileCols * m_tileRows;
    if (grid->childCount() != want) {   // size changed: new tiles, all uploaded
        while (QSGNode *n = grid->firstChild()) {
            grid->removeChildNode(n);
            delete n;
        }
        for (int i = 0; i < want; ++i) {
            QSGImageNode *img = window()->createImageNode();
            img->setFiltering(QSGTexture::Linear);
            img->setOwnsTexture(true);
            grid->appendChildNode(img);
        }
        std::fill(m_tileDirty.begin(), m_tileDirty.end(), 1);
    }
    if (want == 0)
        return root;
    const QRectF target = targetRect();
    const double sx = target.width() / m_frame.width(), sy = target.height() / m_frame.height();
    QElapsedTimer t;
    t.start();
    int uploaded = 0;
    int i = 0;
    for (QSGNode *n = grid->firstChild(); n; n = n->nextSibling(), ++i) {
        auto *img = static_cast<QSGImageNode *>(n);
        const int tx = i % m_tileCols, ty = i / m_tileCols;
        const QRect tile = QRect(tx * kTile, ty * kTile, kTile, kTile).intersected(m_frame.rect());
        if (m_tileDirty[size_t(i)] || !img->texture()) {
            img->setTexture(window()->createTextureFromImage(m_frame.copy(tile), QQuickWindow::TextureIsOpaque));
            m_tileDirty[size_t(i)] = 0;
            ++uploaded;
        }
        img->setRect(QRectF(target.x() + tile.x() * sx, target.y() + tile.y() * sy, tile.width() * sx, tile.height() * sy));
        img->setSourceRect(QRectF(QPointF(0, 0), tile.size()));
    }
    if (m_textureDirty) {
        m_textureDirty = false;
        m_statUploadNs += t.nsecsElapsed();
        m_statTiles += uploaded;
        ++m_statFrames;
        if (!m_statClock.isValid())
            m_statClock.start();
        else if (m_statClock.elapsed() >= 5000) {
            const double sec = m_statClock.elapsed() / 1000.0;
            qInfo("view: %.0f s: %d frames (%.0f/s) from %d updates touching it, %d refreshes, %.1f of %d tiles uploaded per frame, %.1f ms each, %dx%d",
                  sec, m_statFrames, m_statFrames / sec, m_statUpdates, m_statRefreshes, double(m_statTiles) / m_statFrames,
                  want, m_statUploadNs / 1e6 / m_statFrames, m_frame.width(), m_frame.height());
            m_statFrames = m_statUpdates = m_statRefreshes = m_statTiles = 0;
            m_statUploadNs = 0;
            m_statClock.restart();
        }
    }
    return root;
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

quint32 RdpView::keysymFor(const QKeyEvent *e)
{
#ifdef Q_OS_MACOS
    // Modifiers by physical key (Qt swaps Cmd and Ctrl in Qt::Key): Cmd is Super (Command on a Mac).
    switch (e->nativeVirtualKey()) {
    case 55: return 0xffeb;  case 54: return 0xffec;   // Command
    case 56: return 0xffe1;  case 60: return 0xffe2;   // Shift
    case 58: return 0xffe9;  case 61: return 0xffea;   // Option = Alt
    case 59: return 0xffe3;  case 62: return 0xffe4;   // Control
    case 57: return 0xffe5;                            // Caps Lock
    default: break;
    }
#endif
    switch (e->key()) {
    case Qt::Key_Escape: return 0xff1b;
    case Qt::Key_Tab: case Qt::Key_Backtab: return 0xff09;
    case Qt::Key_Backspace: return 0xff08;
    case Qt::Key_Return: return 0xff0d;
    case Qt::Key_Enter: return 0xff8d;
    case Qt::Key_Insert: return 0xff63;
    case Qt::Key_Delete: return 0xffff;
    case Qt::Key_Home: return 0xff50;
    case Qt::Key_End: return 0xff57;
    case Qt::Key_PageUp: return 0xff55;
    case Qt::Key_PageDown: return 0xff56;
    case Qt::Key_Left: return 0xff51;
    case Qt::Key_Up: return 0xff52;
    case Qt::Key_Right: return 0xff53;
    case Qt::Key_Down: return 0xff54;
    case Qt::Key_Shift: return 0xffe1;
    case Qt::Key_Control: return 0xffe3;
    case Qt::Key_Alt: return 0xffe9;
    case Qt::Key_Meta: return 0xffeb;
    case Qt::Key_CapsLock: return 0xffe5;
    default: break;
    }
    if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F35)
        return 0xffbe + quint32(e->key() - Qt::Key_F1);
    // The character the key produces, so the remote side gets it whatever its keyboard layout.
    const QString text = e->text();
    if (!text.isEmpty() && text.at(0).unicode() >= 0x20 && text.at(0).unicode() != 0x7f
        && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
        const char32_t cp = text.toUcs4().value(0);
        return cp <= 0xff ? quint32(cp) : 0x01000000u | quint32(cp);
    }
    // With Cmd/Ctrl the text is a control code or empty: send the plain key (Cmd+C → Super + c).
    const int k = e->key();
    if (k >= Qt::Key_A && k <= Qt::Key_Z)
        return quint32('a' + (k - Qt::Key_A));
    if (k >= 0x20 && k < 0x7f)
        return quint32(k);
    return 0;
}

void RdpView::keyPressEvent(QKeyEvent *e)
{
    if (m_session && m_session->usesKeysyms()) {
        // The same keysym goes up as went down, even if modifiers changed in between.
        const quint32 native = e->nativeVirtualKey() ? e->nativeVirtualKey() : quint32(e->key());
        quint32 ks = m_keysymsDown.value(native);
        if (!ks)
            ks = keysymFor(e);
        if (ks) {
            m_keysymsDown.insert(native, ks);
            m_session->sendKeysym(ks, true);
        }
        e->accept();
        return;
    }
    // Auto-repeat is sent as repeated key-downs, like a real keyboard.
    if (m_session)
        if (const unsigned sc = scancodeFor(e))
            m_session->sendKey(sc, true);
    e->accept();
}

void RdpView::keyReleaseEvent(QKeyEvent *e)
{
    if (m_session && m_session->usesKeysyms()) {
        if (!e->isAutoRepeat()) {
            const quint32 native = e->nativeVirtualKey() ? e->nativeVirtualKey() : quint32(e->key());
            if (const quint32 ks = m_keysymsDown.take(native))
                m_session->sendKeysym(ks, false);
        }
        e->accept();
        return;
    }
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
    m_keysymsDown.clear();
    QQuickItem::focusOutEvent(e);
}
