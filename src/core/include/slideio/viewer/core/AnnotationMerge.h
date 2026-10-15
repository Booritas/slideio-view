#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <vector>

namespace slideio::viewer::core
{

/// What a merge did, so the result can be reported rather than assumed.
struct MergeOutcome
{
    std::vector<Annotation> annotations;
    /// Ids only the incoming set had.
    int adopted = 0;
    /// Ids in both, where the incoming one was newer and replaced the other.
    int superseded = 0;
    /// Ids in both, where the current one was newer or the two tied.
    int kept = 0;
};

/// Unions two annotation sets by id.
///
/// Where both sets hold an id, the later `modifiedAt` survives. **The
/// comparison truncates both sides to whole seconds**, because that is the
/// precision the file format carries: formatIso8601Utc casts to
/// std::chrono::seconds, so an annotation that has been to disk has lost its
/// sub-second part and one still in memory has not. Comparing them as they are
/// would favour whichever happens to be in memory and would give a different
/// answer after a round-trip. Merging the same two sets twice must agree with
/// itself. On a tie `current` wins: it is what the user is looking at.
///
/// Order is preserved deliberately, because index order is paint order and
/// hit-test order. `current` keeps its sequence, a superseded annotation's
/// replacement takes the slot the old one held, and ids only `incoming` had are
/// appended.
///
/// An id repeated inside one input is left alone: both copies survive and only
/// the first is matched. That is malformed input, not a case to silently
/// "repair" by dropping a clinical finding.
///
/// Nothing is deduplicated by geometry. Two identical shapes under different
/// ids are two findings, possibly from two readers.
[[nodiscard]] MergeOutcome mergeAnnotations(const std::vector<Annotation>& current,
                                            const std::vector<Annotation>& incoming);

} // namespace slideio::viewer::core
