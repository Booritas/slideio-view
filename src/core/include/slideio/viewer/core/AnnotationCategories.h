#pragma once

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
/// case-insensitive duplicates keeping the FIRST spelling seen. Order is
/// otherwise preserved and is never sorted: categoryTally() displays tallies in
/// list order, and a list that sorted itself would reorder the counting rows
/// behind the user mid-count.
[[nodiscard]] std::vector<std::string> normalizeCategoryList(
    const std::vector<std::string>& raw);

} // namespace slideio::viewer::core
