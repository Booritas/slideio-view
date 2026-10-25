#pragma once

class QWidget;

namespace slideio::viewer::ui
{

// Gives the window a one-pixel native border whenever it is full screen, so
// menus and dialogs still appear above it. Call once, before the first
// showFullScreen(); a no-op off Windows.
//
// Windows treats a borderless OpenGL window covering the screen as exclusive
// full screen and stops compositing other top-level windows over it: the menu
// popup opens behind the viewport and the menu bar looks dead. Qt documents
// the border as the workaround ("Fullscreen OpenGL Based Windows"); in Qt 6 it
// is the private QWindowsWindow::setHasBorderInFullScreen, which is why the
// implementation alone reaches Qt's private headers.
void keepPopupsAboveFullScreen(QWidget* window);

} // namespace slideio::viewer::ui
