#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <QString>

namespace slideio::viewer::ui
{

/// The user's word for an annotation's shape: "Rectangle", "Polygon" or
/// "Freehand Line". Used by the viewport's undo descriptions, by MainWindow
/// when it labels a deletion requested from the list panel, and by the list
/// panel's Type column.
///
/// One std::visit over the geometry variant, in one place. A new geometry type
/// fails to compile here until it is named, which is the point -- three copies
/// of this dispatch would be three chances to ship a shape the user sees
/// labelled as nothing at all.
QString shapeName(const core::Annotation& annotation);

} // namespace slideio::viewer::ui
