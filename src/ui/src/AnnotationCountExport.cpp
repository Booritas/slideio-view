#include "slideio/viewer/ui/AnnotationCountExport.h"

#include "slideio/viewer/core/Iso8601.h"
#include "slideio/viewer/ui/CsvFormat.h"

#include <algorithm>
#include <string>

namespace slideio::viewer::ui
{

std::string categoryCountCsv(const std::vector<CategoryTallyRow>& rows,
                             const CountExportContext& context,
                             std::chrono::system_clock::time_point exportedAt)
{
    // Formatted once: every row carries the same instant, and a second call
    // could straddle a second boundary and write two of them.
    const std::string provenance = csvProvenance(context.slidePath, context.sceneIndex, context.slideId,
                                                 core::formatIso8601Utc(exportedAt));

    std::string csv = kCsvUtf8ByteOrderMark;
    // Lower-case, underscore-separated column names: this file is read by
    // spreadsheets AND by scripts, and a header a script has to normalise is a
    // header that will be normalised differently twice.
    csv += "category,count,in_list,slide,scene,slide_id,exported_at";
    csv += kCsvLineEnding;

    for (const CategoryTallyRow& row : rows) {
        // The synthetic row is written as the word Unclassified with in_list
        // false, and needs no column of its own: core::isUnclassifiedCategory
        // refuses it as a configured category, as a rename source and as a
        // rename target, so nothing else can ever appear under that name.
        csv += csvField(row.category);
        csv += ',';
        csv += std::to_string(row.count);
        csv += ',';
        csv += row.configured ? "true" : "false";
        csv += ',';
        csv += provenance;
        csv += kCsvLineEnding;
    }
    return csv;
}

std::string sceneExportFileStem(const std::string& slideFileName, int sceneIndex)
{
    // Mirrors the annotation file's own naming -- "<something>.s<N>" -- so an
    // export sits beside the annotations it came from and sorts next to them.
    std::string stem;
    stem.reserve(slideFileName.size() + 16);
    for (const char c : slideFileName) {
        switch (c) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            stem.push_back('_');
            break;
        default:
            // Control characters included: a file name arrives from the shell
            // and the dialog would simply refuse one carrying them.
            stem.push_back(static_cast<unsigned char>(c) < 0x20 ? '_' : c);
            break;
        }
    }
    // Trailing dots and spaces are legal to type and illegal to create on
    // Windows, and a name ending in one is silently truncated rather than
    // refused.
    while (!stem.empty() && (stem.back() == '.' || stem.back() == ' ')) {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "slide";
    }
    return stem + ".s" + std::to_string(sceneIndex);
}

std::string countExportFileStem(const std::string& slideFileName, int sceneIndex)
{
    return sceneExportFileStem(slideFileName, sceneIndex) + ".counts";
}

} // namespace slideio::viewer::ui
