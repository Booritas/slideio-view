#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationCategories.h"

using slideio::viewer::core::caseFoldKey;
using slideio::viewer::core::normalizeCategoryList;

TEST_CASE("normalizeCategoryList trims and drops blanks", "[core][Categories]")
{
    const std::vector<std::string> out =
        normalizeCategoryList({"  Tumor  ", "", "   ", "Stroma"});

    REQUIRE(out == std::vector<std::string>{"Tumor", "Stroma"});
}

TEST_CASE("normalizeCategoryList drops case-insensitive duplicates keeping the first spelling",
          "[core][Categories]")
{
    const std::vector<std::string> out =
        normalizeCategoryList({"Mitosis", "MITOSIS", "mitosis"});

    REQUIRE(out == std::vector<std::string>{"Mitosis"});
}

TEST_CASE("normalizeCategoryList preserves the order given", "[core][Categories]")
{
    // Never sorted: categoryTally displays in list order, so a list that
    // sorted itself would reorder the counting rows behind the user.
    const std::vector<std::string> out =
        normalizeCategoryList({"Zeta", "Alpha", "Mitosis"});

    REQUIRE(out == std::vector<std::string>{"Zeta", "Alpha", "Mitosis"});
}

TEST_CASE("normalizeCategoryList on an empty or all-blank list gives an empty list",
          "[core][Categories]")
{
    REQUIRE(normalizeCategoryList({}).empty());
    REQUIRE(normalizeCategoryList({"", "  ", "\t"}).empty());
}

TEST_CASE("normalizeCategoryList refuses Unclassified in any case or padding", "[core][Categories]")
{
    // "Unclassified" is the absence of a classification, not a category. A
    // configured one would collide with the synthetic tally row of that name.
    REQUIRE(normalizeCategoryList({"Unclassified", "unclassified", "  UNCLASSIFIED  "}).empty());
    REQUIRE(normalizeCategoryList({"Tumor", "Unclassified", "Stroma"})
            == std::vector<std::string>{"Tumor", "Stroma"});
}

TEST_CASE("The default category list is FR-ANN-13's five", "[core][Categories]")
{
    REQUIRE(slideio::viewer::core::kDefaultAnnotationCategories
            == std::vector<std::string>{"Tumor", "Stroma", "Necrosis", "Normal", "Artifact"});
}

#include "slideio/viewer/core/Annotation.h"

#include <chrono>

using slideio::viewer::core::Annotation;
using slideio::viewer::core::AnnotationProperties;
using slideio::viewer::core::PointF;
using slideio::viewer::core::PointGeometry;
using slideio::viewer::core::retagClassification;

namespace
{

Annotation markerWith(const std::string& id, const std::string& classification)
{
    Annotation annotation(id, PointGeometry{PointF{1.0, 2.0}});
    AnnotationProperties properties = annotation.properties();
    properties.classification = classification;
    annotation.setProperties(std::move(properties));
    return annotation;
}

constexpr std::chrono::system_clock::time_point kStamp{std::chrono::seconds{1'700'000'000}};

} // namespace

TEST_CASE("retagClassification matches case-insensitively and rewrites the classification",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "mitosis"),
                                              markerWith("b", "MITOSIS")};

    const auto changes = retagClassification(annotations, "Mitosis", "Mitotic figure", kStamp);

    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].id == "a");
    REQUIRE(changes[0].before.properties().classification == "mitosis");
    REQUIRE(changes[0].after.properties().classification == "Mitotic figure");
    REQUIRE(changes[1].after.properties().classification == "Mitotic figure");
}

TEST_CASE("retagClassification leaves non-matching annotations out of the result entirely",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis"),
                                              markerWith("b", "Lymphocyte")};

    const auto changes = retagClassification(annotations, "Mitosis", "Mitotic figure", kStamp);

    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].id == "a");
}

TEST_CASE("retagClassification stamps modifiedAt from its parameter, not the clock",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis")};

    const auto changes = retagClassification(annotations, "Mitosis", "Other", kStamp);

    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].after.metadata().modifiedAt == kStamp);
    // The before state keeps whatever it had, so undo restores it verbatim.
    REQUIRE(changes[0].before.metadata().modifiedAt
            == annotations[0].metadata().modifiedAt);
    REQUIRE(changes[0].after.metadata().createdAt == annotations[0].metadata().createdAt);
}

TEST_CASE("retagClassification renaming a category to itself changes nothing",
          "[core][Categories]")
{
    // Review Focus 1: the user opens Rename, edits nothing, presses OK. An
    // empty result means the caller records no undo step, and a step that does
    // nothing is worse than no step because the user assumes it worked.
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis")};

    REQUIRE(retagClassification(annotations, "Mitosis", "Mitosis", kStamp).empty());
    REQUIRE(retagClassification(annotations, "Mitosis", "mitosis", kStamp).empty());
}

TEST_CASE("retagClassification refuses an empty or blank target", "[core][Categories]")
{
    // Otherwise a rename to "" would silently unclassify every matching
    // annotation as one undo step.
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis")};

    REQUIRE(retagClassification(annotations, "Mitosis", "", kStamp).empty());
    REQUIRE(retagClassification(annotations, "Mitosis", "   ", kStamp).empty());
}

TEST_CASE("retagClassification trims its target", "[core][Categories]")
{
    // Review Focus 2: an untrimmed target can never match the configured list,
    // so it would show as its own tally row forever.
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis")};

    const auto changes = retagClassification(annotations, "Mitosis", "  Mitotic figure  ", kStamp);

    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].after.properties().classification == "Mitotic figure");
}

TEST_CASE("retagClassification finds nothing when no annotation carries the category",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "Lymphocyte")};

    REQUIRE(retagClassification(annotations, "Mitosis", "Other", kStamp).empty());
}

TEST_CASE("retagClassification refuses Unclassified as a target or a source, in any case",
          "[core][Categories]")
{
    CHECK(slideio::viewer::core::isUnclassifiedCategory(" UNCLASSIFIED "));
    CHECK_FALSE(slideio::viewer::core::isUnclassifiedCategory("Tumor"));
    for (const char* name : {"Unclassified", "unclassified", " UnClassified "}) {
        const std::vector<Annotation> carrying{markerWith("a", "Mitosis"), markerWith("b", name)};
        CHECK(retagClassification(carrying, "Mitosis", name, kStamp).empty());
        CHECK(retagClassification(carrying, name, "Mitosis", kStamp).empty());
    }
}

TEST_CASE("retagClassification matches a stored classification with surrounding whitespace",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", " Mitosis "), markerWith("b", "\tmitosis")};

    const auto changes = retagClassification(annotations, "Mitosis", "Mitotic figure", kStamp);

    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].before.properties().classification == " Mitosis ");
    REQUIRE(changes[0].after.properties().classification == "Mitotic figure");
    REQUIRE(changes[1].after.properties().classification == "Mitotic figure");
}

// --- caseFoldKey -----------------------------------------------------------
//
// Every non-ASCII literal below is written as explicit UTF-8 bytes. Spelling
// them as characters would make the test depend on how the compiler reads this
// file's encoding, which is the one thing these cases must not be sensitive to.

TEST_CASE("caseFoldKey folds ASCII", "[core][Categories]")
{
    REQUIRE(caseFoldKey("Mitosis") == "mitosis");
    REQUIRE(caseFoldKey("MITOSIS") == "mitosis");
    REQUIRE(caseFoldKey("") == "");
}

TEST_CASE("caseFoldKey folds the Latin-1 Supplement", "[core][Categories]")
{
    // "Zellkoerper" with an o-diaeresis: U+00D6 is \xC3\x96, U+00F6 is \xC3\xB6.
    // Two spellings of one German category must produce one key, or the tally
    // shows two rows and a rename of one of them misses the other's
    // annotations.
    const std::string upper = "ZELLK\xC3\x96RPER";
    const std::string lower = "Zellk\xC3\xB6rper";

    REQUIRE(caseFoldKey(upper) == caseFoldKey(lower));
    REQUIRE(caseFoldKey(upper) == "zellk\xC3\xB6rper");
}

TEST_CASE("caseFoldKey leaves the multiplication sign alone", "[core][Categories]")
{
    // U+00D7 sits inside the Latin-1 letter run but is not a letter. Shifting
    // it by 0x20 would turn it into the division sign.
    REQUIRE(caseFoldKey("\xC3\x97") == "\xC3\x97");
}

TEST_CASE("caseFoldKey folds Latin Extended-A across all three stretches",
          "[core][Categories]")
{
    REQUIRE(caseFoldKey("\xC4\x80") == "\xC4\x81"); // U+0100 -> U+0101, even/odd
    REQUIRE(caseFoldKey("\xC5\x81") == "\xC5\x82"); // U+0141 -> U+0142, odd/even
    REQUIRE(caseFoldKey("\xC5\xB9") == "\xC5\xBA"); // U+0179 -> U+017A, odd/even
}

TEST_CASE("caseFoldKey handles the two codepoints that leave their stretch",
          "[core][Categories]")
{
    REQUIRE(caseFoldKey("\xC5\xB8") == "\xC3\xBF"); // U+0178 -> U+00FF, backwards
    REQUIRE(caseFoldKey("\xC5\xBF") == "s");        // U+017F long s -> ASCII s
}

TEST_CASE("caseFoldKey leaves eszett alone", "[core][Categories]")
{
    // Simple folding, as Qt's toCaseFolded does it: eszett is its own fold, so
    // "STRASSE" and the eszett spelling are two categories, not one.
    REQUIRE(caseFoldKey("\xC3\x9F") == "\xC3\x9F");
}

TEST_CASE("caseFoldKey passes through what it cannot fold", "[core][Categories]")
{
    // Cyrillic: outside the table, so it folds to itself -- narrower than
    // Qt, and identical on both sides of the layer boundary, which is the
    // property that matters.
    const std::string cyrillic = "\xD0\x9C\xD0\xB8";
    REQUIRE(caseFoldKey(cyrillic) == cyrillic);

    // A lone continuation byte is not valid UTF-8 at all. It must come back
    // unchanged rather than be decoded as if it were a letter.
    REQUIRE(caseFoldKey("A\x96") == "a\x96");

    // A truncated two-byte sequence at the very end of the string: the lead is
    // in the Latin range but there is no continuation byte to pair it with.
    REQUIRE(caseFoldKey("x\xC3") == "x\xC3");
}

TEST_CASE("normalizeCategoryList deduplicates a non-ASCII case pair", "[core][Categories]")
{
    // The defect this exists for: before caseFoldKey, these were two
    // vocabulary entries while the tally -- folding with Qt -- showed them as
    // one row.
    const std::vector<std::string> out =
        normalizeCategoryList({"Zellk\xC3\xB6rper", "ZELLK\xC3\x96RPER"});

    REQUIRE(out == std::vector<std::string>{"Zellk\xC3\xB6rper"});
}
