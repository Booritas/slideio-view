#pragma once

#include <QString>

namespace slideio::viewer::ui
{

// Directory where the application stores log files. Returned path uses the
// platform's standard per-user writable location:
//   Windows: %LOCALAPPDATA%\SlideIO\SlideIO Viewer\logs
//   macOS:   ~/Library/Application Support/SlideIO/SlideIO Viewer/logs
//   Linux:   $XDG_DATA_HOME/SlideIO/SlideIO Viewer/logs
//            (typically ~/.local/share/SlideIO/SlideIO Viewer/logs)
// Caller may need to create the directory before writing.
QString logDirectory();

// Full path to the primary log file (within logDirectory()).
QString logFilePath();

} // namespace slideio::viewer::ui
