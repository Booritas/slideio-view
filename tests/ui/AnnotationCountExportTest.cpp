#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationCountExport.h"
#include "slideio/viewer/core/Iso8601.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

std::chrono::system_clock::time_point testInstant()
{
    std::chrono::system_clock::time_point when{};
    REQUIRE(core::parseIso8601Utc("2026-10-20T09:15:00Z", when));
    return when;
}

ui::CountExportContext testContext()
{
    ui::CountExportContext context;
    context.slidePath = "D:/slides/Philips-1.tiff";
    context.slideId = "a1b2c3d4e5f60718";
    context.sceneIndex = 0;
    return context;
}

/// The file split on CRLF, with the trailing empty element dropped. Every row
/// this function writes is terminated rather than separated, so the split
/// always leaves one.
std::vector<std::string> lines(const std::string& csv)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < csv.size()) {
        const std::size_t at = csv.find("\r\n", start);
        if (at == std::string::npos) {
            out.push_back(csv.substr(start));
            break;
        }
        out.push_back(csv.substr(start, at - start));
        start = at + 2;
    }
    return out;
}

std::size_t countColumn(const std::string& row)
{
    // Column 1 of a row whose first field is known not to be quoted.
    const std::size_t first = row.find(',');
    REQUIRE(first != std::string::npos);
    const std::size_t second = row.find(',', first + 1);
    REQUIRE(second != std::string::npos);
    return static_cast<std::size_t>(std::stoul(row.substr(first + 1, second - first - 1)));
}

} // namespace

TEST_CASE("categoryCountCsv starts with a UTF-8 BOM", "[ui][AnnotationCountExport]")
{
    // Without it Excel reads a .csv in the machine's legacy codepage, so a
    // category spelled in Greek or Cyrillic -- both of which the fold now
    // treats as first-class -- arrives as mojibake on a double-click.
    const std::string csv = ui::categoryCountCsv({}, testContext(), testInstant());

    REQUIRE(csv.rfind("\xEF\xBB\xBF", 0) == 0);
}

TEST_CASE("categoryCountCsv writes a header even with nothing counted", "[ui][AnnotationCountExport]")
{
    // An empty scene is a legitimate export: a count of zero is a finding, and
    // a file with no header would not say what it failed to find.
    const std::string csv = ui::categoryCountCsv({}, testContext(), testInstant());
    const std::vector<std::string> rows = lines(csv);

    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front() == "\xEF\xBB\xBF" "category,count,in_list,slide,scene,slide_id,exported_at");
}

TEST_CASE("categoryCountCsv ends every row with CRLF", "[ui][AnnotationCountExport]")
{
    const std::vector<ui::CategoryTallyRow> tally{{"Tumor", 10, true, false}};

    const std::string csv = ui::categoryCountCsv(tally, testContext(), testInstant());

    REQUIRE(csv.size() >= 2);
    REQUIRE(csv.substr(csv.size() - 2) == "\r\n");
    // And no bare LF anywhere, which is what a half-applied line ending looks
    // like.
    REQUIRE(csv.find('\n') != std::string::npos);
    for (std::size_t i = 0; i < csv.size(); ++i) {
        if (csv[i] == '\n') {
            REQUIRE(i > 0);
            REQUIRE(csv[i - 1] == '\r');
        }
    }
}

TEST_CASE("categoryCountCsv keeps the tally's order", "[ui][AnnotationCountExport]")
{
    // The file is the strip the user was reading. categoryTally orders
    // configured categories first in list order, then unconfigured ones in
    // first-appearance order, then Unclassified -- and never by count, because
    // a row that moves under a clicking hand is a misclick. A file that
    // reordered it would not be the same read-out.
    const std::vector<ui::CategoryTallyRow> tally{
        {"Tumor", 2, true, false},
        {"Stroma", 40, true, false},
        {"CD31", 7, false, false},
        {"Unclassified", 15, false, true},
    };

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, testContext(), testInstant()));

    REQUIRE(rows.size() == 5);
    REQUIRE(rows[1].rfind("Tumor,2,true,", 0) == 0);
    REQUIRE(rows[2].rfind("Stroma,40,true,", 0) == 0);
    REQUIRE(rows[3].rfind("CD31,7,false,", 0) == 0);
    REQUIRE(rows[4].rfind("Unclassified,15,false,", 0) == 0);
}

TEST_CASE("categoryCountCsv writes no total row", "[ui][AnnotationCountExport]")
{
    // One row per tally row and nothing else. A total line is the classic thing
    // that silently becomes a data row when the file is read back, and every
    // annotation already lands in exactly one row, so the column sums to the
    // scene's annotation count without one.
    const std::vector<ui::CategoryTallyRow> tally{
        {"Tumor", 10, true, false}, {"Stroma", 3, true, false}, {"Unclassified", 2, false, true}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, testContext(), testInstant()));

    REQUIRE(rows.size() == 4);
    std::size_t sum = 0;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        sum += countColumn(rows[i]);
    }
    REQUIRE(sum == 15);
}

TEST_CASE("categoryCountCsv repeats the provenance on every row", "[ui][AnnotationCountExport]")
{
    // Per row rather than in a header block, so two exports can be pasted one
    // under the other and every row still says which slide and scene it
    // counted. slide_id and scene are also what tie a count back to the
    // annotation file, which is keyed by exactly that pair.
    ui::CountExportContext context = testContext();
    context.sceneIndex = 3;
    const std::vector<ui::CategoryTallyRow> tally{{"Tumor", 1, true, false}, {"Stroma", 2, true, false}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, context, testInstant()));

    REQUIRE(rows.size() == 3);
    for (std::size_t i = 1; i < rows.size(); ++i) {
        REQUIRE(rows[i].find(",D:/slides/Philips-1.tiff,3,a1b2c3d4e5f60718,2026-10-20T09:15:00Z") != std::string::npos);
    }
}

TEST_CASE("categoryCountCsv writes one timestamp, not one per row", "[ui][AnnotationCountExport]")
{
    // Formatted once. Two calls could straddle a second boundary and write a
    // file that claims to have been exported at two different instants.
    const std::vector<ui::CategoryTallyRow> tally{
        {"A", 1, true, false}, {"B", 1, true, false}, {"C", 1, true, false}};

    const std::string csv = ui::categoryCountCsv(tally, testContext(), testInstant());

    const std::string stamp = "2026-10-20T09:15:00Z";
    std::size_t found = 0;
    for (std::size_t at = csv.find(stamp); at != std::string::npos; at = csv.find(stamp, at + 1)) {
        ++found;
    }
    REQUIRE(found == 3);
    const std::vector<std::string> rows = lines(csv);
    for (std::size_t i = 1; i < rows.size(); ++i) {
        REQUIRE(rows[i].size() == rows[1].size());
    }
}

TEST_CASE("categoryCountCsv quotes a category holding a comma", "[ui][AnnotationCountExport]")
{
    const std::vector<ui::CategoryTallyRow> tally{{"Tumor, invasive", 4, true, false}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, testContext(), testInstant()));

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[1].rfind("\"Tumor, invasive\",4,true,", 0) == 0);
}

TEST_CASE("categoryCountCsv doubles a quote inside a category", "[ui][AnnotationCountExport]")
{
    const std::vector<ui::CategoryTallyRow> tally{{"\"Tumor\"", 1, true, false}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, testContext(), testInstant()));

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[1].rfind("\"\"\"Tumor\"\"\",1,true,", 0) == 0);
}

TEST_CASE("categoryCountCsv keeps a category holding a newline on one row",
          "[ui][AnnotationCountExport]")
{
    // A classification reaches us from a hand-editable JSON file, so it can
    // hold anything. Quoted, an embedded newline is one field of one row; bare,
    // it would split into two rows and shift every later column.
    const std::vector<ui::CategoryTallyRow> tally{{"Tumor\nStroma", 2, false, false},
                                                  {"Unclassified", 1, false, true}};

    const std::string csv = ui::categoryCountCsv(tally, testContext(), testInstant());
    const std::vector<std::string> rows = lines(csv);

    // Split naively on CRLF the embedded LF does not break a line, so the row
    // count is still header + 2.
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[1].rfind("\"Tumor\nStroma\",2,false,", 0) == 0);
}

TEST_CASE("categoryCountCsv defuses a category a spreadsheet would evaluate",
          "[ui][AnnotationCountExport]")
{
    // A category is whatever the user typed into the counting combo, and Excel,
    // LibreOffice and Sheets all evaluate a cell beginning with = + - or @. The
    // file is meant to be mailed, so the conventional apostrophe goes on.
    const std::vector<ui::CategoryTallyRow> tally{{"=SUM(A1:A9)", 1, false, false},
                                                 {"+Tumor", 2, false, false},
                                                 {"-Stroma", 3, false, false},
                                                 {"@Necrosis", 4, false, false}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, testContext(), testInstant()));

    REQUIRE(rows.size() == 5);
    REQUIRE(rows[1].rfind("'=SUM(A1:A9),1,", 0) == 0);
    REQUIRE(rows[2].rfind("'+Tumor,2,", 0) == 0);
    REQUIRE(rows[3].rfind("'-Stroma,3,", 0) == 0);
    REQUIRE(rows[4].rfind("'@Necrosis,4,", 0) == 0);
}

TEST_CASE("categoryCountCsv leaves an ordinary category alone", "[ui][AnnotationCountExport]")
{
    // The escaping must not fire on the common case: a plain name is written as
    // itself, with no quotes and no apostrophe.
    const std::vector<ui::CategoryTallyRow> tally{{"Tumor", 10, true, false}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, testContext(), testInstant()));

    REQUIRE(rows[1].rfind("Tumor,10,true,", 0) == 0);
}

TEST_CASE("categoryCountCsv writes a non-ASCII category as its own UTF-8 bytes",
          "[ui][AnnotationCountExport]")
{
    // Pass-through, byte for byte. The fold is a comparison key and never
    // reaches a file; what the user typed is what is counted and what is
    // written.
    const std::vector<ui::CategoryTallyRow> tally{{"Zellk\xC3\xB6rper", 3, false, false},
                                                 {"\xD0\x9E\xD0\xBF\xD1\x83\xD1\x85\xD0\xBE\xD0\xBB\xD1\x8C", 5,
                                                  false, false}};

    const std::string csv = ui::categoryCountCsv(tally, testContext(), testInstant());

    REQUIRE(csv.find("Zellk\xC3\xB6rper,3,") != std::string::npos);
    REQUIRE(csv.find("\xD0\x9E\xD0\xBF\xD1\x83\xD1\x85\xD0\xBE\xD0\xBB\xD1\x8C,5,") != std::string::npos);
}

TEST_CASE("categoryCountCsv quotes a slide path holding a comma", "[ui][AnnotationCountExport]")
{
    // Provenance is user data too: a slide can sit in a folder with a comma in
    // its name, and an unquoted one would shift every column after it.
    ui::CountExportContext context = testContext();
    context.slidePath = "D:/cases/Smith, J/slide.svs";
    const std::vector<ui::CategoryTallyRow> tally{{"Tumor", 1, true, false}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, context, testInstant()));

    REQUIRE(rows[1].find(",\"D:/cases/Smith, J/slide.svs\",0,") != std::string::npos);
}

TEST_CASE("categoryCountCsv writes an empty slide id as an empty field",
          "[ui][AnnotationCountExport]")
{
    // An unidentifiable slide gives an empty id, which is the same contract
    // ViewportWidget::currentSlideId() already has. The column stays, empty, so
    // the row still parses and still says the id was not available.
    ui::CountExportContext context = testContext();
    context.slideId.clear();
    const std::vector<ui::CategoryTallyRow> tally{{"Tumor", 1, true, false}};

    const std::vector<std::string> rows = lines(ui::categoryCountCsv(tally, context, testInstant()));

    REQUIRE(rows[1] == "Tumor,1,true,D:/slides/Philips-1.tiff,0,,2026-10-20T09:15:00Z");
}

TEST_CASE("countExportFileStem keys the name by scene, as the annotation file is",
          "[ui][AnnotationCountExport]")
{
    // Annotations are "<slideId>.s<N>.annotations.json" because a scene has its
    // own coordinate space; a counts file counts one scene and is named to sort
    // beside it.
    REQUIRE(ui::countExportFileStem("Philips-1.tiff", 0) == "Philips-1.tiff.s0.counts");
    REQUIRE(ui::countExportFileStem("Philips-1.tiff", 3) == "Philips-1.tiff.s3.counts");
}

TEST_CASE("countExportFileStem replaces what a file system refuses",
          "[ui][AnnotationCountExport]")
{
    // The slide's name arrives from the shell and can hold anything; the stem
    // only has to be a name the save dialog will accept as its suggestion.
    REQUIRE(ui::countExportFileStem("a:b*c?d\"e<f>g|h", 1) == "a_b_c_d_e_f_g_h.s1.counts");
    REQUIRE(ui::countExportFileStem("deep/dir\\slide", 0) == "deep_dir_slide.s0.counts");
}

TEST_CASE("countExportFileStem drops trailing dots and spaces", "[ui][AnnotationCountExport]")
{
    // Legal to type, illegal to create on Windows, and silently truncated
    // rather than refused.
    REQUIRE(ui::countExportFileStem("slide. .", 0) == "slide.s0.counts");
    REQUIRE(ui::countExportFileStem("   ", 0) == "slide.s0.counts");
}

TEST_CASE("countExportFileStem falls back to a name rather than none",
          "[ui][AnnotationCountExport]")
{
    // An empty suggestion makes a save dialog open on no name at all, which
    // reads as the dialog having failed.
    REQUIRE(ui::countExportFileStem("", 2) == "slide.s2.counts");
}
