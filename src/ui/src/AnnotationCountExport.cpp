#include "slideio/viewer/ui/AnnotationCountExport.h"

#include "slideio/viewer/core/Iso8601.h"

#include <algorithm>
#include <string>

namespace slideio::viewer::ui
{

namespace
{

/// RFC 4180's line ending. Excel and every spreadsheet read either, but the
/// format says CRLF and this file's whole job is to be read by something else.
constexpr const char* kLineEnding = "\r\n";

/// Prepended to the file so Excel reads it as UTF-8 rather than the machine's
/// legacy codepage. Without it a category spelled in Greek or Cyrillic -- which
/// core::caseFoldKey now folds, so they are first-class here -- arrives as
/// mojibake on a double-click, and the user's only recourse is the import
/// wizard they do not know exists.
constexpr const char* kUtf8ByteOrderMark = "\xEF\xBB\xBF";

/// True when a spreadsheet would read this field as a formula rather than as
/// text.
///
/// Excel, LibreOffice and Google Sheets all evaluate a cell beginning with one
/// of these, so a category named "=cmd" or "+SUM(A1)" -- and a category is
/// whatever the user typed into the counting combo -- becomes executable
/// content in a file that is going to be mailed to somebody. The leading tab
/// and carriage return are in the list because Excel strips them and then reads
/// what follows.
bool looksLikeFormula(const std::string& field)
{
    if (field.empty()) {
        return false;
    }
    switch (field.front()) {
    case '=':
    case '+':
    case '-':
    case '@':
    case '\t':
    case '\r':
        return true;
    default:
        return false;
    }
}

/// One CSV field, quoted and escaped.
///
/// Quoted whenever it holds a comma, a quote, a CR or an LF, with internal
/// quotes doubled, which is all RFC 4180 asks. A field a spreadsheet would
/// evaluate gets a leading apostrophe as well -- the conventional mitigation,
/// and it goes on before quoting, so a field that needs quoting anyway keeps
/// the apostrophe as one character of its own data. That does mean a category
/// genuinely named "-Tumor" reads back with an apostrophe: the trade is
/// deliberate, because this file exists to be opened in a spreadsheet and a
/// formula is the worse of the two surprises.
std::string field(const std::string& value)
{
    std::string text = value;
    if (looksLikeFormula(text)) {
        text.insert(text.begin(), '\'');
    }
    const bool mustQuote = text.find_first_of(",\"\r\n") != std::string::npos;
    if (!mustQuote) {
        return text;
    }
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('"');
    for (const char c : text) {
        if (c == '"') {
            quoted.push_back('"');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

} // namespace

std::string categoryCountCsv(const std::vector<CategoryTallyRow>& rows,
                             const CountExportContext& context,
                             std::chrono::system_clock::time_point exportedAt)
{
    // Formatted once: every row carries the same instant, and a second call
    // could straddle a second boundary and write two of them.
    const std::string timestamp = core::formatIso8601Utc(exportedAt);
    const std::string scene = std::to_string(context.sceneIndex);
    const std::string slide = field(context.slidePath);
    const std::string slideId = field(context.slideId);
    const std::string when = field(timestamp);

    std::string csv = kUtf8ByteOrderMark;
    // Lower-case, underscore-separated column names: this file is read by
    // spreadsheets AND by scripts, and a header a script has to normalise is a
    // header that will be normalised differently twice.
    csv += "category,count,in_list,slide,scene,slide_id,exported_at";
    csv += kLineEnding;

    for (const CategoryTallyRow& row : rows) {
        // The synthetic row is written as the word Unclassified with in_list
        // false, and needs no column of its own: core::isUnclassifiedCategory
        // refuses it as a configured category, as a rename source and as a
        // rename target, so nothing else can ever appear under that name.
        csv += field(row.category);
        csv += ',';
        csv += std::to_string(row.count);
        csv += ',';
        csv += row.configured ? "true" : "false";
        csv += ',';
        csv += slide;
        csv += ',';
        csv += scene;
        csv += ',';
        csv += slideId;
        csv += ',';
        csv += when;
        csv += kLineEnding;
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
