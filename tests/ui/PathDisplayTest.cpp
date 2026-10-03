#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/PathDisplay.h"

using namespace slideio::viewer::ui;

TEST_CASE("slideDisplayName uses the file name of a slide file", "[ui][PathDisplay]")
{
    REQUIRE(slideDisplayName(QStringLiteral("D:/cases/42/tumour.svs")) == QStringLiteral("tumour.svs"));
}

TEST_CASE("slideDisplayName uses the folder name of a DICOM directory", "[ui][PathDisplay]")
{
    REQUIRE(slideDisplayName(QStringLiteral("D:/cases/study-1234")) == QStringLiteral("study-1234"));
}

TEST_CASE("slideDisplayName ignores a trailing separator", "[ui][PathDisplay]")
{
    // getExistingDirectory normally strips it, but a path typed by hand, passed
    // on the command line, or dropped from another application may keep it.
    REQUIRE(slideDisplayName(QStringLiteral("D:/cases/study-1234/")) == QStringLiteral("study-1234"));
    REQUIRE(slideDisplayName(QStringLiteral("D:\\cases\\study-1234\\")) == QStringLiteral("study-1234"));
}

TEST_CASE("slideDisplayName falls back to the whole path when there is no name", "[ui][PathDisplay]")
{
    // A drive root has no trailing component; showing an empty loading overlay
    // would be worse than showing the root itself.
    REQUIRE(slideDisplayName(QStringLiteral("D:/")) == QStringLiteral("D:/"));
}

TEST_CASE("slideDisplayName handles backslash-separated paths", "[ui][PathDisplay]")
{
    REQUIRE(slideDisplayName(QStringLiteral("D:\\cases\\42\\tumour.svs")) == QStringLiteral("tumour.svs"));
}

TEST_CASE("slideDisplayName leaves an empty path empty", "[ui][PathDisplay]")
{
    REQUIRE(slideDisplayName(QString()).isEmpty());
}
