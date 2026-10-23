#include "slideio/viewer/ui/CsvFormat.h"

namespace slideio::viewer::ui
{

namespace
{

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

} // namespace

std::string csvField(const std::string& value)
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

std::string csvProvenance(const std::string& slidePath, int sceneIndex, const std::string& slideId,
                          const std::string& exportedAtIso8601)
{
    return csvField(slidePath) + ',' + std::to_string(sceneIndex) + ',' + csvField(slideId) + ','
         + csvField(exportedAtIso8601);
}

} // namespace slideio::viewer::ui
