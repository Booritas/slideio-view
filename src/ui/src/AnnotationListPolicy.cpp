#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include "slideio/viewer/core/AnnotationCategories.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <string>
#include <utility>

namespace slideio::viewer::ui
{

namespace
{

std::string toLower(const std::string& text)
{
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

std::string trimmedText(const std::string& text)
{
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto first = std::find_if(text.begin(), text.end(), notSpace);
    const auto last = std::find_if(text.rbegin(), text.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool containsFold(const std::string& haystack, const std::string& needleLower)
{
    return toLower(haystack).find(needleLower) != std::string::npos;
}

/// Everything a row needs plus the creation index the display label is built
/// from. Kept beside the row through the sort so the number cannot drift.
struct IndexedRow
{
    AnnotationRow row;
    std::size_t creationIndex;
    std::chrono::system_clock::time_point modifiedAt;
};

} // namespace

std::vector<AnnotationRow> annotationRows(const std::vector<core::Annotation>& annotations,
                                          const std::string& filterText, AnnotationSortKey sort,
                                          const AnnotationTypeNames& typeName)
{
    const std::string needle = toLower(filterText);

    std::vector<IndexedRow> indexed;
    indexed.reserve(annotations.size());

    for (std::size_t i = 0; i < annotations.size(); ++i) {
        const core::Annotation& annotation = annotations[i];
        const core::AnnotationProperties& properties = annotation.properties();

        const std::string type = typeName(annotation.type());

        // Compute display label first: it's the fallback for filtering if there's
        // no stored label.
        const std::string displayLabel = properties.label.empty() ? type + " " + std::to_string(i + 1)
                                                                  : properties.label;

        // Filter against display label and classification, not notes or type.
        if (!needle.empty() && !containsFold(displayLabel, needle)
            && !containsFold(properties.classification, needle)) {
            continue;
        }

        AnnotationRow row;
        row.id = annotation.id();
        row.label = displayLabel;
        row.type = type;
        row.classification = properties.classification;

        indexed.push_back(IndexedRow{std::move(row), i, annotation.metadata().modifiedAt});
    }

    const auto byCreation = [](const IndexedRow& a, const IndexedRow& b) {
        return a.creationIndex < b.creationIndex;
    };

    switch (sort) {
    case AnnotationSortKey::CreationOrder:
        std::sort(indexed.begin(), indexed.end(), byCreation);
        break;
    case AnnotationSortKey::Label:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.row.label != b.row.label) {
                return a.row.label < b.row.label;
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::Classification:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.row.classification != b.row.classification) {
                return a.row.classification < b.row.classification;
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::Type:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.row.type != b.row.type) {
                return a.row.type < b.row.type;
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::ModifiedTime:
        // Most recent first: the mark someone just touched is the one they are
        // looking for.
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (a.modifiedAt != b.modifiedAt) {
                return a.modifiedAt > b.modifiedAt;
            }
            return byCreation(a, b);
        });
        break;
    }

    std::vector<AnnotationRow> rows;
    rows.reserve(indexed.size());
    for (IndexedRow& entry : indexed) {
        rows.push_back(std::move(entry.row));
    }
    return rows;
}

std::string annotationDockTitle(std::size_t total, std::size_t shown)
{
    if (shown == total) {
        return "Annotations (" + std::to_string(total) + ")";
    }
    return "Annotations (" + std::to_string(shown) + " of " + std::to_string(total) + ")";
}

std::vector<CategoryTallyRow> categoryTally(const std::vector<core::Annotation>& annotations,
                                            const std::vector<std::string>& categories)
{
    std::vector<CategoryTallyRow> rows;
    std::vector<std::string> keys;  // Parallel to rows, lowercased for matching.
    for (const std::string& category : categories) {
        const std::string clean = trimmedText(category);
        if (clean.empty()) {
            continue;
        }
        const std::string key = toLower(clean);
        if (core::isUnclassifiedCategory(clean)) {
            continue;
        }
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
            continue;
        }
        keys.push_back(key);
        rows.push_back(CategoryTallyRow{clean, 0, true});
    }

    std::size_t unclassified = 0;
    for (const core::Annotation& annotation : annotations) {
        const std::string clean = trimmedText(annotation.properties().classification);
        if (clean.empty() || core::isUnclassifiedCategory(clean)) {
            ++unclassified;
            continue;
        }
        const std::string key = toLower(clean);
        const auto found = std::find(keys.begin(), keys.end(), key);
        if (found != keys.end()) {
            ++rows[static_cast<std::size_t>(std::distance(keys.begin(), found))].count;
            continue;
        }
        // Not configured: it joins the end, keeping the spelling first seen.
        keys.push_back(key);
        rows.push_back(CategoryTallyRow{clean, 1, false});
    }

    if (unclassified > 0) {
        rows.push_back(CategoryTallyRow{"Unclassified", unclassified, false, true});
    }
    return rows;
}

} // namespace slideio::viewer::ui
