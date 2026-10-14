#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace slideio::viewer::ui
{

/// What the list's sort combo offers.
enum class AnnotationSortKey
{
    CreationOrder,
    Label,
    Classification,
    Type,
    ModifiedTime,
};

/// One row of the annotation list, already formatted. The panel copies these
/// straight into its tree; it makes no further decisions about text or order.
struct AnnotationRow
{
    std::string id;
    std::string label;
    std::string type;
    std::string classification;
};

/// The user's word for a geometry type. Passed in rather than looked up here
/// because the real one (ui::shapeName) returns a translated QString, and this
/// function has to stay free of Qt to be worth testing -- no test binary
/// creates a QApplication, so anything needing one is untestable by
/// construction.
using AnnotationTypeNames = std::function<std::string(core::AnnotationType)>;

/// The rows to display, filtered and sorted.
///
/// `filterText` is matched case-insensitively as a substring of the label and
/// the classification, and of nothing else: a row matching on its notes would
/// show nothing resembling the search text in any visible column.
///
/// An annotation with no label displays as "<Type> N", where N is its 1-based
/// position in `annotations` -- that is, in creation order, across every
/// annotation on the slide. Not across the filtered rows and not across the
/// sorted ones: a shape that renames itself when the user types in the search
/// box or changes the sort is a shape they cannot keep track of.
///
/// Every sort breaks ties by creation order, so two rows with equal keys never
/// swap between rebuilds -- and the list rebuilds on every model change.
[[nodiscard]] std::vector<AnnotationRow> annotationRows(
    const std::vector<core::Annotation>& annotations, const std::string& filterText,
    AnnotationSortKey sort, const AnnotationTypeNames& typeName);

/// "Annotations (12)", or "Annotations (3 of 12)" when a filter is hiding some
/// -- so a filtered list is never mistaken for an empty slide.
[[nodiscard]] std::string annotationDockTitle(std::size_t total, std::size_t shown);

} // namespace slideio::viewer::ui
