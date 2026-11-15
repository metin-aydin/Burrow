#include "platform/WindowChrome.h"

#include <QWindow>

#import <AppKit/AppKit.h>

namespace WindowChrome {

void useContentTitleBar(QWindow *window)
{
    if (!window)
        return;
    // Qt keeps these through its own style updates: content under the title bar, no title bar fill.
    window->setFlags(window->flags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
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

} // namespace WindowChrome
