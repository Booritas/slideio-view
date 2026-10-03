#pragma once

#include <QString>

namespace slideio::viewer::ui
{

// Returns the short label to show for a slide path: the file name for a slide
// file, the folder name for a DICOM directory. Trailing separators are ignored,
// and a path with no trailing component (a drive root) is returned whole rather
// than reduced to an empty string.
QString slideDisplayName(const QString& path);

} // namespace slideio::viewer::ui
