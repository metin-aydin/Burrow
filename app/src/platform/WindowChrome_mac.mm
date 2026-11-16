#include "platform/WindowChrome.h"

#include <QEvent>
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace WindowChrome {

namespace {
void applyToNativeWindow(QWindow *window)
{
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    NSWindow *nsWindow = view.window;
    if (!nsWindow)
        return;
    nsWindow.titleVisibility = NSWindowTitleHidden;
    nsWindow.titlebarAppearsTransparent = YES;
    // An empty unified toolbar makes the title bar 52 pt tall and centres the traffic lights in it.
    if (!nsWindow.toolbar) {
        NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"BurrowMainToolbar"];
        toolbar.showsBaselineSeparator = NO;
        nsWindow.toolbar = toolbar;
    }
    nsWindow.toolbarStyle = NSWindowToolbarStyleUnified;
}
// Qt can replace the NSWindow behind a QWindow without telling the QWindow (when it is shown after a flag
// change, moves to another screen, comes back from the tray...), and the new NSWindow shows its title over our
// header. Re-applying on every expose catches all of those; the settings are idempotent and cheap.
class ChromeKeeper : public QObject
{
public:
    explicit ChromeKeeper(QWindow *window) : QObject(window), m_window(window) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window && (event->type() == QEvent::Expose || event->type() == QEvent::Show
                                    || event->type() == QEvent::WindowStateChange))
            applyToNativeWindow(m_window);
        return false;
    }

private:
    QWindow *m_window;
};
// Watches every window showing or hiding and switches the app between a regular app (Dock icon) and an
// accessory (menu bar only).
class DockKeeper : public QObject
{
public:
    using QObject::QObject;

    static void update()
    {
        bool anyOpen = false;
        for (QWindow *w : QGuiApplication::topLevelWindows()) {
            const Qt::WindowType type = w->type();
            if (w->isVisible() && (type == Qt::Window || type == Qt::Dialog))
                anyOpen = true;
        }
        const NSApplicationActivationPolicy wanted =
            anyOpen ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory;
        if (NSApp.activationPolicy == wanted)
            return;
        [NSApp setActivationPolicy:wanted];
        if (anyOpen)
            [NSApp activateIgnoringOtherApps:YES];   // back from the menu bar: bring the window forward
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) && watched->isWindowType())
            QMetaObject::invokeMethod(this, &DockKeeper::update, Qt::QueuedConnection);
        return false;
    }
};
} // namespace

void dockIconFollowsWindows()
{
    qApp->installEventFilter(new DockKeeper(qApp));
}

void useContentTitleBar(QWindow *window)
{
    if (!window)
        return;
    // Qt keeps these through its own style updates: content under the title bar, no title bar fill.
    window->setFlags(window->flags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
    applyToNativeWindow(window);
    QTimer::singleShot(0, window, [window] { applyToNativeWindow(window); });
    window->installEventFilter(new ChromeKeeper(window));
}

} // namespace WindowChrome
