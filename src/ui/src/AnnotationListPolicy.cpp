#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include <algorithm>
#include <cctype>
#include <chrono>

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

        if (!needle.empty() && !containsFold(properties.label, needle)
            && !containsFold(properties.classification, needle)) {
            continue;
        }

        const std::string type = typeName ? typeName(annotation.type()) : std::string{"Shape"};

        AnnotationRow row;
        row.id = annotation.id();
        // The number is the position on the slide, not in this list: it is
        // computed before any filtering or sorting touches the order.
        row.label = properties.label.empty() ? type + " " + std::to_string(i + 1)
                                             : properties.label;
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

} // namespace slideio::viewer::ui
