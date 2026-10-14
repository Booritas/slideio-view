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

/// The user's word for a geometry type, for callers that have an
/// AnnotationType rather than a whole annotation. Shares its strings with
/// shapeName() so the two cannot diverge, and returns an empty string for an
/// unhandled type rather than a plausible-looking fallback -- a blank Type
/// column is visibly wrong, where "Shape" reads as intentional.
QString shapeTypeName(core::AnnotationType type);

} // namespace slideio::viewer::ui
