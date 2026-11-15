#pragma once

class QWindow;

// Lets the window's content run under the title bar (our own header with the traffic lights in it),
// like Claude and other modern Mac apps. The title bar is 52 pt tall with the buttons centred in it.
// No-op on other platforms.
namespace WindowChrome {
void useContentTitleBar(QWindow *window);

// macOS: the Dock icon shows only while one of Burrow's windows is open, so closing the window leaves Burrow
// in the menu bar alone (as on Windows, where a hidden window has no taskbar button). No-op elsewhere.
void dockIconFollowsWindows();
}
