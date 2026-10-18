#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationCategories.h"

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
