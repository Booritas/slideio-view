#include "slideio/viewer/core/AnnotationCategories.h"

#include <algorithm>
#include <cctype>

namespace slideio::viewer::core
{
namespace
{

std::string toLowerAscii(const std::string& text)
{
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

std::string trimmed(const std::string& text)
{
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto first = std::find_if(text.begin(), text.end(), notSpace);
    const auto last = std::find_if(text.rbegin(), text.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}

} // namespace

bool isUnclassifiedCategory(const std::string& text)
{
    return toLowerAscii(trimmed(text)) == "unclassified";
}

std::vector<std::string> normalizeCategoryList(const std::vector<std::string>& raw)
{
    std::vector<std::string> result;
    std::vector<std::string> seen;
    for (const std::string& entry : raw) {
        const std::string clean = trimmed(entry);
        if (clean.empty() || isUnclassifiedCategory(clean)) {
            continue;
        }
        const std::string key = toLowerAscii(clean);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        result.push_back(clean);
    }
    return result;
}

std::vector<RetagChange> retagClassification(const std::vector<Annotation>& annotations,
                                             const std::string& from, const std::string& to,
                                             std::chrono::system_clock::time_point modifiedAt)
{
    const std::string target = trimmed(to);
    const std::string fromKey = toLowerAscii(trimmed(from));
    if (fromKey.empty() || target.empty() || fromKey == toLowerAscii(target)
        || isUnclassifiedCategory(target) || isUnclassifiedCategory(from)) {
        // Nothing to do, and nothing to record. An empty target would silently
        // unclassify every match as one undo step, so it is refused here, where a
        // test can reach it. "Unclassified" is refused on both sides: as a target
        // it would write a string categoryTally folds into the synthetic row, and
        // as a source it would miss every empty one. Renaming a category to itself
        // must not leave a step on the undo stack that does nothing.
        return {};
    }

    std::vector<RetagChange> changes;
    for (const Annotation& source : annotations) {
        if (toLowerAscii(trimmed(source.properties().classification)) != fromKey) {
            continue;
        }
        AnnotationProperties properties = source.properties();
        properties.classification = target;
        AnnotationMetadata metadata = source.metadata();
        metadata.modifiedAt = modifiedAt;
        // The four-argument constructor, never setProperties(): that one reads
        // the system clock and restamps modifiedAt, which would make this
        // function impure and break the exactness `after` promises.
        changes.push_back(RetagChange{source.id(), source,
                                      Annotation{source.id(), source.geometry(),
                                                 std::move(properties), metadata}});
    }
    return changes;
}

} // namespace slideio::viewer::core
