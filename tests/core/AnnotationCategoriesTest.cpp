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

TEST_CASE("The default category list is FR-ANN-13's five", "[core][Categories]")
{
    REQUIRE(slideio::viewer::core::kDefaultAnnotationCategories
            == std::vector<std::string>{"Tumor", "Stroma", "Necrosis", "Normal", "Artifact"});
}
