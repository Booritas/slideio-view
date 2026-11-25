#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/ResultsAttachment.h"

using namespace slideio::viewer::core;

TEST_CASE("SameSlidePathFoldsCaseOnWindows", "[core][ResultsAttachment]")
{
#ifdef _WIN32
    CHECK(sameSlidePath("C:\\Slides\\a.svs", "c:/slides/A.svs"));
#else
    CHECK_FALSE(sameSlidePath("C:\\Slides\\a.svs", "c:/slides/A.svs"));
#endif
}

TEST_CASE("SameSlidePathNormalisesDots", "[core][ResultsAttachment]")
{
    CHECK(sameSlidePath("/a/b/../c.svs", "/a/c.svs"));
    CHECK_FALSE(sameSlidePath("/a/b/c.svs", "/a/c.svs"));
}

TEST_CASE("SameSlidePathEmptyEqualsNothing", "[core][ResultsAttachment]")
{
    CHECK_FALSE(sameSlidePath("", ""));
    CHECK_FALSE(sameSlidePath("", "/a/c.svs"));
}

TEST_CASE("SameSlidePathReadsNonAsciiAsUtf8", "[core][ResultsAttachment]")
{
    // "/Präparate/Schnitt_1.svs", "/Präparate/Schnitt_2.svs" and a Japanese
    // folder, in UTF-8: on Windows these are not in the ANSI code page's terms.
    const std::string german = "/Pr\xC3\xA4parate/Schnitt_1.svs";
    const std::string germanOther = "/Pr\xC3\xA4parate/Schnitt_2.svs";
    const std::string japanese = "/\xE6\xA8\x99\xE6\x9C\xAC/\xE5\x88\x87\xE7\x89\x87.svs";
    const std::string japaneseOther = "/\xE6\xA8\x99\xE6\x9C\xAC/\xE5\x88\x87\xE7\x89\x87\xE4\xBA\x8C.svs";
    CHECK(sameSlidePath(german, german));
    CHECK(sameSlidePath(japanese, japanese));
    CHECK_FALSE(sameSlidePath(german, germanOther));
    CHECK_FALSE(sameSlidePath(japanese, japaneseOther));
    CHECK_FALSE(sameSlidePath(german, japanese));
    CHECK(sameSlidePath("/Pr\xC3\xA4parate/x/../Schnitt_1.svs", german));
}

TEST_CASE("SameSlidePathIgnoresTrailingSeparator", "[core][ResultsAttachment]")
{
    CHECK(sameSlidePath("/a/workspace/", "/a/workspace"));
    CHECK(sameSlidePath("/a/workspace//", "/a/workspace"));
    CHECK(sameSlidePath("/", "/"));
    CHECK_FALSE(sameSlidePath("/a/workspace/", "/a"));
#ifdef _WIN32
    CHECK(sameSlidePath("C:\\Results\\ws\\", "c:/results/ws"));
    CHECK(sameSlidePath("C:\\", "c:/"));
#endif
}

TEST_CASE("decideAttachment covers every branch", "[core][ResultsAttachment]")
{
    const ResultsProvenance ws{"/slides/a.svs", 1};
    CHECK(decideAttachment(ws, "/slides/a.svs", 0, "") == AttachmentDecision::RefuseScene);
    CHECK(decideAttachment(ws, "/other/a.svs", 0, "/other/a.svs") == AttachmentDecision::RefuseScene);
    CHECK(decideAttachment(ws, "/slides/a.svs", 1, "") == AttachmentDecision::Attach);
    CHECK(decideAttachment(ws, "/moved/a.svs", 1, "/moved/a.svs") == AttachmentDecision::Attach);
    CHECK(decideAttachment(ws, "/moved/a.svs", 1, "") == AttachmentDecision::WarnSlideMoved);
    // A remembered override for another path still warns.
    CHECK(decideAttachment(ws, "/moved/a.svs", 1, "/elsewhere/a.svs") == AttachmentDecision::WarnSlideMoved);
}
