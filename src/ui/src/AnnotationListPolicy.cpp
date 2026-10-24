#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include "slideio/viewer/core/AnnotationCategories.h"

#include <QtCore/qnamespace.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

namespace slideio::viewer::ui
{

namespace
{

/// Caseless-matching key for a label or classification.
///
/// core::caseFoldKey rather than QString::toCaseFolded, although Qt is right
/// here and available: core folds the vocabulary list and the rename, this
/// folds the tally and the filter, and the two must answer the same. Folding
/// here with Qt and there with std::tolower is what made the tally merge two
/// spellings of one category into a row of eight that a rename of that
/// category then changed five of.
///
/// Qt's fold is still wider -- it has the whole Unicode table, where
/// caseFoldKey has Latin, Greek and Cyrillic. Agreement across the layer
/// boundary is worth more than the width, and a script with no case folds to
/// itself under both.
///
/// It is deliberately not "lowercasing": case folding is what Unicode defines
/// for caseless comparison.
std::string toLower(const std::string& text)
{
    return core::caseFoldKey(text);
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

    // Case-folded, because the filter above is: a list filtered without regard
    // to case and ordered with regard to it disagrees with itself, and every
    // capitalised label collected above every lowercase one -- "Zebra" before
    // "apple". Folding makes two spellings equal rather than ordered, so each
    // sort below still falls through to byCreation, which is what keeps rows
    // from swapping places between rebuilds.
    const auto foldedLess = [](const std::string& a, const std::string& b) {
        return toLower(a) < toLower(b);
    };
    const auto foldedEqual = [](const std::string& a, const std::string& b) {
        return toLower(a) == toLower(b);
    };

    switch (sort) {
    case AnnotationSortKey::CreationOrder:
        std::sort(indexed.begin(), indexed.end(), byCreation);
        break;
    case AnnotationSortKey::Label:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (!foldedEqual(a.row.label, b.row.label)) {
                return foldedLess(a.row.label, b.row.label);
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::Classification:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (!foldedEqual(a.row.classification, b.row.classification)) {
                return foldedLess(a.row.classification, b.row.classification);
            }
            return byCreation(a, b);
        });
        break;
    case AnnotationSortKey::Type:
        std::sort(indexed.begin(), indexed.end(), [&](const IndexedRow& a, const IndexedRow& b) {
            if (!foldedEqual(a.row.type, b.row.type)) {
                return foldedLess(a.row.type, b.row.type);
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
    // Key -> index into rows, rather than a second vector held parallel to it.
    // The parallel version maintained its invariant by hand in four places and
    // then broke it on purpose in a fifth: the synthetic Unclassified row was
    // pushed to rows and not to keys, which was harmless only because nothing
    // looked anything up afterwards. A map cannot be one entry out of step.
    std::unordered_map<std::string, std::size_t> indexByKey;
    for (const std::string& category : categories) {
        const std::string clean = core::trimAsciiWhitespace(category);
        if (clean.empty()) {
            continue;
        }
        if (core::isUnclassifiedCategory(clean)) {
            continue;
        }
        // After the refusals, not before: an Unclassified entry is discarded
        // here, and folding its case first was work thrown away.
        const std::string key = toLower(clean);
        if (indexByKey.count(key) != 0) {
            continue;
        }
        indexByKey.emplace(key, rows.size());
        rows.push_back(CategoryTallyRow{clean, 0, true});
    }

    std::size_t unclassified = 0;
    for (const core::Annotation& annotation : annotations) {
        // A text is a note and a ruler or angle a measurement, not findings.
        // Counting one would change a clinical count -- a ruler classified
        // Tumor is a tumour's size, not a second tumour -- and the counts
        // export, built from this same call, with it.
        const core::AnnotationType type = annotation.type();
        if (type == core::AnnotationType::Text || type == core::AnnotationType::Ruler
            || type == core::AnnotationType::Angle) {
            continue;
        }
        const std::string clean = core::trimAsciiWhitespace(annotation.properties().classification);
        if (core::isBlankOrUnclassified(clean)) {
            ++unclassified;
            continue;
        }
        const std::string key = toLower(clean);
        const auto found = indexByKey.find(key);
        if (found != indexByKey.end()) {
            ++rows[found->second].count;
            continue;
        }
        // Not configured: it joins the end, keeping the spelling first seen.
        indexByKey.emplace(key, rows.size());
        rows.push_back(CategoryTallyRow{clean, 1, false});
    }

    if (unclassified > 0) {
        // Deliberately not registered in indexByKey: it is not a category and
        // nothing may ever match into it. Under the parallel vectors this was
        // an off-by-one waiting for the next person to add a lookup.
        rows.push_back(CategoryTallyRow{"Unclassified", unclassified, false, true});
    }
    return rows;
}

bool countingComboSwallowsKey(int key, bool popupVisible)
{
    if (popupVisible) {
        return false;
    }
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        return true;
    default:
        return false;
    }
}

bool countingComboSwallowsWheel(bool popupVisible)
{
    return !popupVisible;
}

CategoryRenameScope categoryRenameScope(bool slideOpen, bool annotationsEditable)
{
    if (!slideOpen) {
        return CategoryRenameScope::VocabularyOnly;
    }
    return annotationsEditable ? CategoryRenameScope::VocabularyAndAnnotations
                               : CategoryRenameScope::Refused;
}

} // namespace slideio::viewer::ui
