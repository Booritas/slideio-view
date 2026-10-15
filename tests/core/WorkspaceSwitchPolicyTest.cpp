#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

using namespace slideio::viewer;

namespace
{

core::WorkspaceSwitchFacts onScreen(int count)
{
    core::WorkspaceSwitchFacts facts;
    facts.currentCount = count;
    facts.currentWritable = true;
    facts.oldPath = "/old/annotations/slide.s0.annotations.json";
    facts.newPath = "/new/annotations/slide.s0.annotations.json";
    return facts;
}

} // namespace

TEST_CASE("nothing on screen asks nothing", "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(0);

    SECTION("and no file in the new folder")
    {
        REQUIRE_FALSE(core::workspaceSwitchOptions(facts).ask);
    }

    SECTION("and a file in the new folder")
    {
        facts.newFileExists = true;
        facts.newCount = 4;
        // Nothing is at risk: there is nothing on screen to lose.
        REQUIRE_FALSE(core::workspaceSwitchOptions(facts).ask);
    }
}

TEST_CASE("on screen, new folder empty: copy or switch without copying",
          "[core][WorkspaceSwitchPolicy]")
{
    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(onScreen(12));

    REQUIRE(options.ask);
    REQUIRE(options.copy);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.merge);
    REQUIRE_FALSE(options.keepCurrent);
}

TEST_CASE("on screen, new folder has a readable file: merge, keep, or use new",
          "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.newFileExists = true;
    facts.newCount = 5;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.merge);
    REQUIRE(options.keepCurrent);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.copy);
}

TEST_CASE("an unparseable file in the new folder withholds every write",
          "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.newFileExists = true;
    facts.newFileReadable = false;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.copy);
    REQUIRE_FALSE(options.keepCurrent);
    REQUIRE_FALSE(options.merge);
}

TEST_CASE("read-only annotations are not offered for writing",
          "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.currentWritable = false;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.copy);
    REQUIRE_FALSE(options.keepCurrent);
    REQUIRE_FALSE(options.merge);
}

TEST_CASE("a failed flush never withholds an option", "[core][WorkspaceSwitchPolicy]")
{
    // The user may be switching away precisely because the old folder could not
    // be written. Copy is the rescue, not a thing to take away.
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.flushFailed = true;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.copy);
}
