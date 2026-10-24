#pragma once

#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/AnnotationLayer.h"

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
    /// On a locked layer: shown, but nothing about it may change.
    bool locked = false;
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
///
/// With `layers`, rows on a HIDDEN layer are omitted -- the list is a view of
/// what is shown, and a teaching layer is hidden precisely so its answers are
/// not read -- and rows on a locked layer say so. Empty `layers` filters
/// nothing.
[[nodiscard]] std::vector<AnnotationRow> annotationRows(
    const std::vector<core::Annotation>& annotations, const std::string& filterText,
    AnnotationSortKey sort, const AnnotationTypeNames& typeName,
    const std::vector<core::AnnotationLayer>& layers = {});

/// How many of the annotations categoryTally counts sit on a hidden layer --
/// for the strip's "Includes n on hidden layers". The tally itself counts them:
/// a clinical count never follows visibility.
[[nodiscard]] std::size_t hiddenLayerCount(const std::vector<core::Annotation>& annotations,
                                           const std::vector<core::AnnotationLayer>& layers);

/// "Annotations (12)", or "Annotations (3 of 12)" when a filter is hiding some
/// -- so a filtered list is never mistaken for an empty slide.
[[nodiscard]] std::string annotationDockTitle(std::size_t total, std::size_t shown);

/// One row of the counting strip's tally.
struct CategoryTallyRow
{
    std::string category;  ///< Display text; "Unclassified" for the empty one.
    std::size_t count;
    bool configured;  ///< In the user's list, as opposed to only in the data.
    bool synthetic = false;  ///< The appended Unclassified row, and only that one.
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

/// Whether the counting combo must swallow this key press rather than let
/// QComboBox act on it. `key` is a Qt::Key value; `popupVisible` says whether
/// the dropdown is open.
///
/// A CLOSED editable QComboBox treats Up, Down, PageUp and PageDown as "step to
/// the next item", replacing whatever the line edit holds. On this combo that
/// silently reassigns the clinical counting category. Group AC26 found it: a
/// category was typed, a marker counted under it, Down was pressed, and the
/// next marker was counted under a different category with nothing announcing
/// the change. The user of a several-hundred-click pass reads the slide and the
/// tally, not the combo, so every click after the bump is miscounted -- and the
/// tally, being correct about what was stored, agrees with the mistake.
///
/// With the popup OPEN the same keys only move the highlight and commit nothing
/// until Enter, so deliberate keyboard selection is untouched: F4 or Alt+Down
/// opens the list, the arrows walk it, Enter picks. That is the gesture this
/// keeps while removing the accident, and it is why the predicate needs to know
/// whether the popup is up rather than refusing the keys outright.
[[nodiscard]] bool countingComboSwallowsKey(int key, bool popupVisible);

/// The same rule for a wheel event: a wheel over a CLOSED combo steps its items
/// exactly as an arrow key does, and a hand resting on a trackpad while the
/// cursor sits over the strip is at least as likely as a bumped arrow key.
/// Over an open popup the wheel scrolls the list and commits nothing.
[[nodiscard]] bool countingComboSwallowsWheel(bool popupVisible);

/// How much of a category rename may go through, given what is open.
enum class CategoryRenameScope
{
    /// A slide is open and its annotations cannot be changed. Nothing happens:
    /// renaming the vocabulary alone would leave the annotations on screen
    /// carrying the old word, so the tally would show the new name with a count
    /// of zero beside an unconfigured row holding the real count -- and the
    /// vocabulary is global, so that damage would follow the user to every
    /// other slide.
    Refused,
    /// No slide is open: rename the list entry, retag nothing. There is nothing
    /// to retag and nothing to record on the undo stack.
    VocabularyOnly,
    /// The ordinary case: retag the matching annotations as one undoable step,
    /// then rename the list entry.
    VocabularyAndAnnotations,
};

/// Whether a rename of a counting category may proceed.
///
/// The category list is a GLOBAL preference, shared by every slide and
/// deliberately off the per-slide undo stack, so renaming an entry with nothing
/// open is a legitimate edit of a preference rather than an edit of a slide.
/// MainWindow used to ask AnnotationPersistenceService::isActive() alone, which
/// is false both for a slide whose annotation file could not be parsed AND for
/// no slide at all -- so renaming a category on an empty viewer answered "These
/// annotations are open read-only and cannot be changed", naming annotations
/// and a slide that were not there. Remove from list, which is vocabulary-only
/// by construction, has never been gated at all; the asymmetry was the tell.
///
/// `annotationsEditable` is only consulted when a slide is open, which is the
/// only time it means anything.
[[nodiscard]] CategoryRenameScope categoryRenameScope(bool slideOpen, bool annotationsEditable);

} // namespace slideio::viewer::ui
