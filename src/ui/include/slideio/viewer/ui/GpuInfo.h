#pragma once

#include <QString>

namespace slideio::viewer::ui
{

// OpenGL strings read from a live context. They can only be queried on the
// thread that holds the context, so ViewportWidget captures them in
// initializeGL() and the About dialog reads the captured copy rather than
// touching GL itself.
//
// Every field is empty when the context never came up (the 3.3 core profile
// was refused, or the widget was never shown). Callers check isValid() and say
// so instead of printing blank rows.
struct GpuInfo
{
    QString vendor;
    QString renderer;
    QString version;
    QString shadingLanguageVersion;

    bool isValid() const { return !renderer.isEmpty(); }
};

} // namespace slideio::viewer::ui
