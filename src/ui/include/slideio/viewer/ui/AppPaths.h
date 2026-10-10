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

// Default directory for the user's annotation workspace:
//   Windows: %USERPROFILE%\Documents\SlideIO Viewer
//   macOS:   ~/Documents/SlideIO Viewer
//   Linux:   $XDG_DOCUMENTS_DIR/SlideIO Viewer
//
// Deliberately NOT an application-data location. Slides often sit on read-only
// institutional storage, so annotations cannot live beside them; application
// data is writable but opaque, and a user who cannot find their annotations
// cannot back them up or send them to a colleague.
QString defaultAnnotationWorkspaceDirectory();

} // namespace slideio::viewer::ui
