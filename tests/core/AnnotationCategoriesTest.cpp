#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationCategories.h"

#include <ios>
#include <string>

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
    // Hebrew: unicameral, so folding it to itself is not a gap -- it is what
    // caseless matching means for a script with no case, and what Qt does with
    // it too.
    const std::string hebrew = "\xD7\x90\xD7\x91";
    REQUIRE(caseFoldKey(hebrew) == hebrew);

    // Chinese, likewise, and three bytes per character rather than two: the
    // decode never runs for it at all.
    const std::string chinese = "\xE7\xB4\xB0\xE8\x83\x9E";
    REQUIRE(caseFoldKey(chinese) == chinese);

    // Armenian DOES have case and is deliberately not covered. Pinned so the
    // boundary is a decision on record rather than something nobody noticed.
    const std::string armenianCapital = "\xD4\xB1";
    REQUIRE(caseFoldKey(armenianCapital) == armenianCapital);

    // A lone continuation byte is not valid UTF-8 at all. It must come back
    // unchanged rather than be decoded as if it were a letter.
    REQUIRE(caseFoldKey("A\x96") == "a\x96");

    // A truncated two-byte sequence at the very end of the string: the lead is
    // in the foldable range but there is no continuation byte to pair it with.
    REQUIRE(caseFoldKey("x\xC3") == "x\xC3");
}

TEST_CASE("caseFoldKey folds the Greek alphabet", "[core][Categories]")
{
    // "MITOSI" in Greek capitals and lowercase: U+039C U+0399 U+03A4 U+039F
    // U+03A3 U+0397 against U+03BC U+03B9 U+03C4 U+03BF U+03C3 U+03B7.
    const std::string upper = "\xCE\x9C\xCE\x99\xCE\xA4\xCE\x9F\xCE\xA3\xCE\x97";
    const std::string lower = "\xCE\xBC\xCE\xB9\xCF\x84\xCE\xBF\xCF\x83\xCE\xB7";

    REQUIRE(caseFoldKey(upper) == caseFoldKey(lower));
    REQUIRE(caseFoldKey(upper) == lower);
}

TEST_CASE("caseFoldKey folds final sigma to ordinary sigma", "[core][Categories]")
{
    // A Greek word ending in sigma is spelled with U+03C2; the same word with
    // anything after it uses U+03C3. Without this they would be two
    // categories, one of them appearing only when the name stands alone.
    REQUIRE(caseFoldKey("\xCF\x82") == "\xCF\x83");
    REQUIRE(caseFoldKey("\xCE\xA3") == "\xCF\x83");
}

TEST_CASE("caseFoldKey folds the accented Greek capitals", "[core][Categories]")
{
    // These sit BELOW the alphabet and fold up into letters scattered across
    // the lowercase range, with no constant offset. Every one is checked
    // because there is no rule to check instead.
    REQUIRE(caseFoldKey("\xCE\x86") == "\xCE\xAC"); // U+0386 -> U+03AC
    REQUIRE(caseFoldKey("\xCE\x88") == "\xCE\xAD"); // U+0388 -> U+03AD
    REQUIRE(caseFoldKey("\xCE\x89") == "\xCE\xAE"); // U+0389 -> U+03AE
    REQUIRE(caseFoldKey("\xCE\x8A") == "\xCE\xAF"); // U+038A -> U+03AF
    REQUIRE(caseFoldKey("\xCE\x8C") == "\xCF\x8C"); // U+038C -> U+03CC
    REQUIRE(caseFoldKey("\xCE\x8E") == "\xCF\x8D"); // U+038E -> U+03CD
    REQUIRE(caseFoldKey("\xCE\x8F") == "\xCF\x8E"); // U+038F -> U+03CE
}

TEST_CASE("caseFoldKey leaves the unassigned Greek slot alone", "[core][Categories]")
{
    // U+03A2 sits between capital rho and capital sigma and is unassigned --
    // lowercase sigma has two forms where uppercase has one. A shift applied
    // across it would invent a letter.
    REQUIRE(caseFoldKey("\xCE\xA2") == "\xCE\xA2");
}

TEST_CASE("caseFoldKey folds the Cyrillic alphabet", "[core][Categories]")
{
    // "MITOZ" in Cyrillic: U+041C U+0418 U+0422 U+041E U+0417 against
    // U+043C U+0438 U+0442 U+043E U+0437.
    const std::string upper = "\xD0\x9C\xD0\x98\xD0\xA2\xD0\x9E\xD0\x97";
    const std::string lower = "\xD0\xBC\xD0\xB8\xD1\x82\xD0\xBE\xD0\xB7";

    REQUIRE(caseFoldKey(upper) == caseFoldKey(lower));
    REQUIRE(caseFoldKey(upper) == lower);
}

TEST_CASE("caseFoldKey folds the Cyrillic letters that sit below the alphabet",
          "[core][Categories]")
{
    // Unicode placed these capitals BEFORE the alphabet and their lowercase
    // forms after it, so the shift is 0x50 rather than 0x20. They are the
    // letters Russian does not use and Ukrainian, Serbian, Macedonian and
    // Belarusian do -- so this is not an exotic corner.
    REQUIRE(caseFoldKey("\xD0\x81") == "\xD1\x91"); // U+0401 YO -> U+0451
    REQUIRE(caseFoldKey("\xD0\x87") == "\xD1\x97"); // U+0407 YI -> U+0457
    REQUIRE(caseFoldKey("\xD0\x8F") == "\xD1\x9F"); // U+040F DZHE -> U+045F
}

TEST_CASE("caseFoldKey folds the paired Cyrillic runs and palochka",
          "[core][Categories]")
{
    REQUIRE(caseFoldKey("\xD1\xA2") == "\xD1\xA3"); // U+0462 -> U+0463, even
    REQUIRE(caseFoldKey("\xD2\x8A") == "\xD2\x8B"); // U+048A -> U+048B, even
    REQUIRE(caseFoldKey("\xD3\x81") == "\xD3\x82"); // U+04C1 -> U+04C2, odd
    REQUIRE(caseFoldKey("\xD3\x90") == "\xD3\x91"); // U+04D0 -> U+04D1, even
    // Palochka's lowercase form is at the far end of its run, not beside it.
    REQUIRE(caseFoldKey("\xD3\x80") == "\xD3\x8F"); // U+04C0 -> U+04CF
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

TEST_CASE("normalizeCategoryList drops a duplicate that matches only after BOTH "
          "trimming and folding", "[core][Categories]")
{
    // Trimming and case folding are each covered on their own above. Neither
    // test would notice a rule that applied one of them and not the other to
    // the comparison key, which is the whole job of this function.
    const std::vector<std::string> out = normalizeCategoryList({"  Tumor  ", "TUMOR"});

    REQUIRE(out == std::vector<std::string>{"Tumor"});
}

TEST_CASE("normalizeCategoryList folds a non-ASCII duplicate that is also padded",
          "[core][Categories]")
{
    const std::vector<std::string> out =
        normalizeCategoryList({" Zellk\xC3\xB6rper ", "ZELLK\xC3\x96RPER"});

    REQUIRE(out == std::vector<std::string>{"Zellk\xC3\xB6rper"});
}

TEST_CASE("caseFoldKey is idempotent across every codepoint it decodes",
          "[core][Categories]")
{
    // A fold is a reduction to a representative, so folding a folded key must
    // change nothing. The sweep is what makes this worth writing: every range
    // and parity rule in the three script blocks is an off-by-one away from
    // mapping a lowercase letter onto a different letter, and that shows up
    // here as a second fold moving something. U+00C0 to U+04FF is exactly the
    // span the decoder claims.
    for (char32_t cp = 0x00C0; cp <= 0x04FF; ++cp) {
        std::string utf8;
        utf8.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));

        const std::string once = caseFoldKey(utf8);
        INFO("codepoint U+" << std::hex << static_cast<unsigned>(cp));
        REQUIRE(caseFoldKey(once) == once);
    }
}

TEST_CASE("caseFoldKey maps one codepoint to one codepoint", "[core][Categories]")
{
    // Simple folding, not full folding: full folding would expand eszett to
    // "ss" and the Greek iota-subscript letters to two codepoints each, giving
    // a key whose length no longer tracks the text it came from.
    //
    // U+017F is the single exception and it is not an expansion: long s folds
    // DOWN to ASCII s, out of the two-byte range into one byte. That is the
    // mapping Unicode gives it, and this sweep found it the first time it ran,
    // which is the case for writing the sweep rather than a handful of
    // examples.
    for (char32_t cp = 0x00C0; cp <= 0x04FF; ++cp) {
        std::string utf8;
        utf8.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));

        const std::size_t expected = (cp == 0x017F) ? 1u : 2u;
        INFO("codepoint U+" << std::hex << static_cast<unsigned>(cp));
        REQUIRE(caseFoldKey(utf8).size() == expected);
    }
}

TEST_CASE("trimAsciiWhitespace trims the six ASCII spaces and nothing else", "[core][AnnotationCategories]")
{
    using slideio::viewer::core::trimAsciiWhitespace;
    REQUIRE(trimAsciiWhitespace(" \t\r\n\f\vTumor \t\r\n\f\v") == "Tumor");
    REQUIRE(trimAsciiWhitespace("  inner  space  ") == "inner  space");
    REQUIRE(trimAsciiWhitespace(" \t ").empty());
    REQUIRE(trimAsciiWhitespace("").empty());
    // A no-break space is not ASCII whitespace, and the lead byte of a UTF-8
    // letter must never be taken for one, whatever the C locale says.
    REQUIRE(trimAsciiWhitespace("\xC2\xA0x") == "\xC2\xA0x");
    REQUIRE(trimAsciiWhitespace(" \xD0\x9E\xD0\xBF ") == "\xD0\x9E\xD0\xBF");
}

TEST_CASE("isBlankOrUnclassified is the tally's rule for no category", "[core][AnnotationCategories]")
{
    using slideio::viewer::core::isBlankOrUnclassified;
    REQUIRE(isBlankOrUnclassified(""));
    REQUIRE(isBlankOrUnclassified(" \t\n"));
    REQUIRE(isBlankOrUnclassified("Unclassified"));
    REQUIRE(isBlankOrUnclassified("  UNCLASSIFIED\t"));
    REQUIRE_FALSE(isBlankOrUnclassified("Tumor"));
    REQUIRE_FALSE(isBlankOrUnclassified(" Unclassified cells"));
}
