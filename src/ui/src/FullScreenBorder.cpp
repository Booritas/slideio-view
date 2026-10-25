#include "slideio/viewer/ui/FullScreenBorder.h"

#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <QtGui/qpa/qplatformwindow_p.h>
#endif

namespace slideio::viewer::ui
{

void keepPopupsAboveFullScreen(QWidget* window)
{
#ifdef Q_OS_WIN
    if (window == nullptr) {
        return;
    }
    // Through Qt rather than SetWindowLongPtr: Qt tracks full screen itself,
    // and a border it did not add shrinks the client area under it, so it
    // concludes the window has left full screen while it still covers the
    // screen -- the action unchecks and Escape and F11 stop working.
    window->winId();
    QWindow* handle = window->windowHandle();
    if (handle == nullptr) {
        return;
    }
    if (auto* native = handle->nativeInterface<QNativeInterface::Private::QWindowsWindow>()) {
        native->setHasBorderInFullScreen(true);
    }
#else
    (void)window;
#endif
}

} // namespace slideio::viewer::ui
