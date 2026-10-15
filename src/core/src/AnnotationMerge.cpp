#include "slideio/viewer/core/AnnotationMerge.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace slideio::viewer::core
{

namespace
{

/// The precision the on-disk format carries. See the header.
std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
toWholeSeconds(std::chrono::system_clock::time_point t)
{
    return std::chrono::time_point_cast<std::chrono::seconds>(t);
}

} // namespace

MergeOutcome mergeAnnotations(const std::vector<Annotation>& current,
                              const std::vector<Annotation>& incoming)
{
    MergeOutcome outcome;
    outcome.annotations = current;

    // First occurrence wins the slot: a repeated id inside `current` leaves its
    // second copy in place, matched by nothing.
    std::unordered_map<std::string, std::size_t> slotById;
    for (std::size_t i = 0; i < outcome.annotations.size(); ++i) {
        slotById.emplace(outcome.annotations[i].id(), i);
    }

    for (const Annotation& candidate : incoming) {
        const auto found = slotById.find(candidate.id());
        if (found == slotById.end()) {
            slotById.emplace(candidate.id(), outcome.annotations.size());
            outcome.annotations.push_back(candidate);
            ++outcome.adopted;
            continue;
        }

        Annotation& held = outcome.annotations[found->second];
        if (toWholeSeconds(candidate.metadata().modifiedAt)
            > toWholeSeconds(held.metadata().modifiedAt)) {
            held = candidate;
            ++outcome.superseded;
        } else {
            ++outcome.kept;
        }
    }

    return outcome;
}

} // namespace slideio::viewer::core
