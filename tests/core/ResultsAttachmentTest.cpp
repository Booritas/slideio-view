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
