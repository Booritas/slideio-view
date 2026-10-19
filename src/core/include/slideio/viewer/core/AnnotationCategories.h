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

/// `text` reduced to a key two spellings of the same category share.
///
/// Simple Unicode case folding over ASCII, the Latin-1 Supplement and Latin
/// Extended-A, applied to UTF-8 bytes; every other byte passes through
/// unchanged. The key is for comparison only and is never displayed -- what
/// the user typed is what is stored and shown.
///
/// It exists because the fold has to be the SAME ONE on both sides of the
/// layer boundary. ui::toLower used std::tolower while ui::categoryTally
/// folded with QString::toCaseFolded, so the tally merged two spellings of a
/// category carrying an o-diaeresis into one row of eight, while
/// normalizeCategoryList kept them as two vocabulary entries and
/// retagClassification renamed only five of the eight. A tally that counts
/// what a rename then misses is worse than either rule alone, so both layers
/// call this one function.
///
/// Coverage stops where the mapping stops being regular: Greek, Cyrillic and
/// the Turkish dotless i fold to themselves, which is what the ASCII-only
/// version did for them as well. Narrower than QString::toCaseFolded, never
/// wider, and identical in core and ui. Eszett folds to itself here, as it
/// does in Qt's simple folding.
[[nodiscard]] std::string caseFoldKey(const std::string& text);

/// True for "Unclassified" in any case, ignoring surrounding whitespace: the
/// absence of a value, never a category. The one definition core uses to
/// refuse it.
[[nodiscard]] bool isUnclassifiedCategory(const std::string& text);

/// The user's category list, cleaned up for storage and display.
///
/// Trims each entry, drops entries that are empty once trimmed, and removes
/// case-insensitive duplicates keeping the FIRST spelling seen. Also drops
/// "Unclassified" in any case: it is the absence of a value, not a category,
/// and a configured one would collide with the synthetic row categoryTally
/// appends for annotations with no classification. Order is otherwise
/// preserved and is never sorted: categoryTally() displays tallies in list
/// order, and a list that sorted itself would reorder the counting rows behind
/// the user mid-count.
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
/// `to` is trimmed. The result is EMPTY, and the caller records no undo step at
/// all, when `to` is empty or blank (it would bulk-unclassify every match as one
/// step), when `from` or `to` is "Unclassified", when `from` is blank, and when
/// the rename is to itself or to the same word in a different case. That is the
/// rule AnnotationEditCommand::create already applies when it refuses to build a
/// command that records nothing.
///
/// `modifiedAt` is a parameter rather than a clock read so this stays pure and
/// testable, as core::shouldAutosave is, and so `after` is the EXACT final
/// state. A redo that re-read the clock would produce a different timestamp
/// from the original action, and undo/redo would stop being exact inverses.
[[nodiscard]] std::vector<RetagChange> retagClassification(
    const std::vector<Annotation>& annotations, const std::string& from, const std::string& to,
    std::chrono::system_clock::time_point modifiedAt);

} // namespace slideio::viewer::core
