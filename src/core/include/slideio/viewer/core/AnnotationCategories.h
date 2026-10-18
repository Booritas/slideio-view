#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <chrono>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// FR-ANN-13's default categories, used when the user has never configured a
/// list. They live in core because core is the only layer both infra (which
/// stores the list) and ui (which displays it) can see -- the layering is
/// strictly downward, so neither of those can read a constant owned by the
/// other. "Unclassified" is deliberately not among them: it is the absence of
/// a value, not a category.
inline const std::vector<std::string> kDefaultAnnotationCategories{
    "Tumor", "Stroma", "Necrosis", "Normal", "Artifact"};

/// The user's category list, cleaned up for storage and display.
///
/// Trims each entry, drops entries that are empty once trimmed, and removes
/// case-insensitive duplicates keeping the FIRST spelling seen. Also drops
/// "Unclassified" in any case: it is the absence of a value, not a category, and
/// a configured one would collide with the synthetic row categoryTally appends
/// for annotations with no classification. Order is
/// otherwise preserved and is never sorted: categoryTally() displays tallies in
/// list order, and a list that sorted itself would reorder the counting rows
/// behind the user mid-count.
[[nodiscard]] std::vector<std::string> normalizeCategoryList(
    const std::vector<std::string>& raw);

/// One annotation's exact before and after state for a retag.
struct RetagChange
{
    std::string id;
    Annotation before;
    Annotation after;
};

/// Every annotation whose classification equals `from` (case-insensitively),
/// paired with the state it should take.
///
/// `to` is trimmed; an empty or blank `to` is refused (it would bulk-unclassify
/// every match as one undo step), as is renaming a category to itself, or to the same word in a
/// different case, yields an EMPTY result, and an empty result means the caller
/// records no undo step at all -- the rule AnnotationEditCommand::create already
/// applies when it refuses to build a command that records nothing.
///
/// `modifiedAt` is a parameter rather than a clock read so this stays pure and
/// testable, as core::shouldAutosave is, and so `after` is the EXACT final
/// state. A redo that re-read the clock would produce a different timestamp
/// from the original action, and undo/redo would stop being exact inverses.
[[nodiscard]] std::vector<RetagChange> retagClassification(
    const std::vector<Annotation>& annotations, const std::string& from, const std::string& to,
    std::chrono::system_clock::time_point modifiedAt);

} // namespace slideio::viewer::core
