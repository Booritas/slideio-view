#pragma once

#include "slideio/viewer/ui/AnnotationListPolicy.h"

#include <chrono>
#include <string>
#include <vector>

namespace slideio::viewer::ui
{

/// Where a counts file's numbers came from, written into every row.
///
/// Repeated per row rather than written once in a header block, so two
/// exports can be pasted one under the other and every row still says which
/// slide and scene it counted. A header block would also have to be a comment,
/// and a comment is the one thing CSV has no agreed syntax for.
struct CountExportContext
{
    /// The slide as the user opened it. Written verbatim, in whatever
    /// separators it arrived with -- it is provenance for a human reader, not
    /// a path anything reopens.
    std::string slidePath;
    /// core::computeSlideId. Empty when the slide could not be identified,
    /// which is the same empty-string contract ViewportWidget::currentSlideId()
    /// already has. The annotation file is keyed by (slideId, sceneIndex), so
    /// these two columns are what tie a count back to the file it came from.
    std::string slideId;
    int sceneIndex = 0;
};

/// The counting tally as a CSV file, ready to write.
///
/// `rows` are categoryTally()'s rows UNCHANGED, in its order: configured
/// categories in list order, then categories only the annotations carry in
/// first-appearance order, then the synthetic Unclassified row. Not sorted
/// here and never sorted by count -- the file is the strip the user was
/// reading, and a file that reordered it would not be.
///
/// Every annotation except a text lands in exactly one row, so the counts sum
/// to the scene's non-text annotation count -- a text is a note, not a
/// finding, and categoryTally leaves it out. A total row is deliberately not
/// written: a spreadsheet sums a column by itself, and a total line is the
/// classic thing that silently becomes a data row when a file is re-read.
///
/// The returned string is the WHOLE file: a UTF-8 byte-order mark, a header
/// row, then one row per tally row, CRLF-terminated as RFC 4180 specifies. The
/// BOM is why a category spelled in Greek or Cyrillic survives a double-click
/// into Excel, which otherwise reads a .csv in the machine's legacy codepage
/// and turns it into mojibake. It is returned rather than prepended by the
/// caller so a test can hold the decision.
///
/// `exportedAt` is a parameter rather than a clock read, as
/// core::retagClassification's `modifiedAt` is: it keeps this pure and lets a
/// test assert the column.
[[nodiscard]] std::string categoryCountCsv(const std::vector<CategoryTallyRow>& rows,
                                           const CountExportContext& context,
                                           std::chrono::system_clock::time_point exportedAt);

/// `<slideFileName>.s<sceneIndex>` with the characters a file system refuses
/// replaced -- the part of a suggested export name both exports share. Each
/// adds its own suffix (`.counts`, `.geojson`), so the two names cannot drift.
[[nodiscard]] std::string sceneExportFileStem(const std::string& slideFileName, int sceneIndex);

/// `name` with the characters a file system refuses replaced, for the default
/// name the save dialog offers.
///
/// Not a general sanitizer and not a security boundary -- the user can type
/// whatever they like in the dialog that follows, and the OS is what refuses
/// it. It exists so the suggested name is one the dialog will accept, since a
/// slide file name reaches us from the shell and may hold anything.
[[nodiscard]] std::string countExportFileStem(const std::string& slideFileName, int sceneIndex);

} // namespace slideio::viewer::ui
