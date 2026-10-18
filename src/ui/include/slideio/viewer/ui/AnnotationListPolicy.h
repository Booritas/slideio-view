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

/// One row of the counting strip's tally.
struct CategoryTallyRow
{
    std::string category;  ///< Display text; "Unclassified" for the empty one.
    std::size_t count;
    bool configured;  ///< In the user's list, as opposed to only in the data.
};

/// The counting tallies, in display order.
///
/// Configured categories first, in `categories` order, INCLUDING those at zero,
/// so a category about to be counted has a row before counting starts. Then
/// categories present in the data but not configured, in first-appearance order
/// over `annotations`. Then Unclassified, only when non-zero.
///
/// There is exactly one Unclassified row, it is always last, it appears only
/// when non-zero, and it can never be a configured category: an entry of
/// `categories` spelled "Unclassified" in any case is skipped, and an annotation
/// whose classification is literally "Unclassified" in any case counts toward
/// the synthetic row. Two rows with one label would leave the context menu
/// unable to tell the synthetic row, which must refuse Rename, from a real one.
///
/// Rows are NEVER ordered by count. A row that jumps while the user is clicking
/// at it is a misclick, and this is a workflow of several hundred clicks -- the
/// same reasoning that makes annotationRows() break every sort tie by creation
/// order so rows never swap between rebuilds.
///
/// Matching is case-insensitive; a configured spelling wins, otherwise the
/// first one seen. A classification that is empty or only whitespace counts as
/// Unclassified: annotation files are hand-editable and a row labelled with
/// three spaces is unreadable.
///
/// It takes no filter text, deliberately: the counts are over every annotation
/// in the scene, never the filtered subset. A clinical count that changed when
/// someone typed in a search box would be a trap, and annotationDockTitle()
/// already distinguishes the two populations.
[[nodiscard]] std::vector<CategoryTallyRow> categoryTally(
    const std::vector<core::Annotation>& annotations,
    const std::vector<std::string>& categories);

} // namespace slideio::viewer::ui
