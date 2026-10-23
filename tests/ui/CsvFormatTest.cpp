#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/CsvFormat.h"

#include <string>

using namespace slideio::viewer::ui;

TEST_CASE("csvField quotes a field holding a comma", "[ui][CsvFormat]")
{
    REQUIRE(csvField("a,b") == "\"a,b\"");
}

TEST_CASE("csvField doubles a quote inside a field", "[ui][CsvFormat]")
{
    REQUIRE(csvField("say \"hi\"") == "\"say \"\"hi\"\"\"");
}

TEST_CASE("csvField defuses a field a spreadsheet would evaluate", "[ui][CsvFormat]")
{
    REQUIRE(csvField("=1+1") == "'=1+1");
    // The apostrophe goes on before quoting, so it is part of the quoted data.
    REQUIRE(csvField("-x,y") == "\"'-x,y\"");
}

TEST_CASE("csvField leaves an ordinary field alone", "[ui][CsvFormat]")
{
    REQUIRE(csvField("plain") == "plain");
    REQUIRE(csvField("").empty());
}

TEST_CASE("csvProvenance writes the four provenance fields in column order", "[ui][CsvFormat]")
{
    REQUIRE(csvProvenance("D:/a,b.svs", 2, "abc", "2026-10-23T09:00:00Z")
            == "\"D:/a,b.svs\",2,abc,2026-10-23T09:00:00Z");
}
