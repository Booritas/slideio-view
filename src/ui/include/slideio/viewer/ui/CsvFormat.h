#pragma once

#include <string>

namespace slideio::viewer::ui
{

/// The conventions every CSV file this application writes shares, in one place
/// so the counts file and the measurements file cannot drift apart.

/// RFC 4180's line ending. Excel and every spreadsheet read either, but the
/// format says CRLF and these files' whole job is to be read by something else.
inline constexpr const char* kCsvLineEnding = "\r\n";

/// Prepended to a file so Excel reads it as UTF-8 rather than the machine's
/// legacy codepage. Without it a category spelled in Greek or Cyrillic -- which
/// core::caseFoldKey folds, so they are first-class here -- arrives as mojibake
/// on a double-click, and the user's only recourse is the import wizard they do
/// not know exists.
inline constexpr const char* kCsvUtf8ByteOrderMark = "\xEF\xBB\xBF";

/// One TEXT field, quoted and escaped.
///
/// Quoted whenever it holds a comma, a quote, a CR or an LF, with internal
/// quotes doubled, which is all RFC 4180 asks. A field a spreadsheet would
/// evaluate gets a leading apostrophe as well -- the conventional mitigation,
/// and it goes on before quoting, so a field that needs quoting anyway keeps
/// the apostrophe as one character of its own data. That does mean a category
/// genuinely named "-Tumor" reads back with an apostrophe: the trade is
/// deliberate, because these files exist to be opened in a spreadsheet and a
/// formula is the worse of the two surprises.
///
/// Text only. A number must never pass through here: a negative one begins
/// with '-' and would be written as the text '-5.
[[nodiscard]] std::string csvField(const std::string& value);

/// The four provenance values both CSV exports end every row with -- slide,
/// scene, slide_id, exported_at -- each already a field, comma-joined, with no
/// leading or trailing comma. Repeated per row rather than written once in a
/// header block, so two exports can be pasted one under the other and every
/// row still says where it came from.
[[nodiscard]] std::string csvProvenance(const std::string& slidePath, int sceneIndex,
                                        const std::string& slideId, const std::string& exportedAtIso8601);

} // namespace slideio::viewer::ui
