#pragma once

#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/AnnotationGeoJson.h"

#include <QString>

#include <string>
#include <vector>

namespace slideio::viewer::ui
{

/// Annotations whose bounding box lies ENTIRELY outside [0, width] x [0, height]
/// -- a scene's level-0 extent. Touching an edge or overhanging it is inside:
/// drawing past the edge is allowed, and only a shape wholly off the scene
/// suggests the file belongs to another image or scene.
[[nodiscard]] int countOutsideScene(const std::vector<core::Annotation>& annotations, double width, double height);

/// Everything the import's one confirmation dialog reports.
struct ImportSummaryInput
{
    /// The chosen file's name, shown in the first line.
    QString fileName;
    /// The reader's result: skip counts and the file's provenance. Not null.
    const core::AnnotationImportResult* result = nullptr;
    /// app::planImport's numbers.
    int toAdd = 0;
    int alreadyPresent = 0;
    /// countOutsideScene over the annotations to add.
    int outsideScene = 0;
    /// The open scene, compared with the file's provenance.
    std::string openSlideId;
    int openScene = 0;
};

/// The whole text of the import's confirmation dialog (design spec, section 3):
/// what will be added, every reason something will not be with its count,
/// and a warning when the file names another slide or scene or its shapes
/// lie off this one. A line whose count is zero is left out, so the dialog
/// for an ordinary import is two lines long.
///
/// Neither warning refuses anything. A file moves legitimately between copies
/// of a slide whose content-derived id differs; what must not happen is the
/// shapes landing on the wrong image without the user being told.
[[nodiscard]] QString importSummary(const ImportSummaryInput& input);

} // namespace slideio::viewer::ui
