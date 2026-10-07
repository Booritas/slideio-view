#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/SlideId.h"
#include "slideio/viewer/core/Types.h"

#include <set>
#include <string>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

SceneInfo makeScene(int index, int width, int height, int numChannels)
{
    SceneInfo s;
    s.index = index;
    s.width = width;
    s.height = height;
    s.numChannels = numChannels;
    return s;
}

std::vector<SceneInfo> twoScenes()
{
    return {makeScene(0, 100000, 80000, 3), makeScene(1, 2048, 1024, 3)};
}

} // namespace

TEST_CASE("computeSlideId is stable and well-formed", "[core][SlideId]")
{
    const std::string id = computeSlideId(twoScenes(), 4096);

    REQUIRE(id.size() == 16);
    REQUIRE(id == computeSlideId(twoScenes(), 4096));
    REQUIRE(id.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST_CASE("computeSlideId changes when any input changes", "[core][SlideId]")
{
    const std::string base = computeSlideId(twoScenes(), 4096);

    REQUIRE(computeSlideId(twoScenes(), 4097) != base);

    auto widthChanged = twoScenes();
    widthChanged[0].width += 1;
    REQUIRE(computeSlideId(widthChanged, 4096) != base);

    auto heightChanged = twoScenes();
    heightChanged[0].height += 1;
    REQUIRE(computeSlideId(heightChanged, 4096) != base);

    auto channelsChanged = twoScenes();
    channelsChanged[0].numChannels += 1;
    REQUIRE(computeSlideId(channelsChanged, 4096) != base);

    // A field of a scene other than the first must count too.
    auto secondSceneChanged = twoScenes();
    secondSceneChanged[1].width += 1;
    REQUIRE(computeSlideId(secondSceneChanged, 4096) != base);

    auto indexChanged = twoScenes();
    indexChanged[1].index = 7;
    REQUIRE(computeSlideId(indexChanged, 4096) != base);

    auto sceneDropped = twoScenes();
    sceneDropped.pop_back();
    REQUIRE(computeSlideId(sceneDropped, 4096) != base);
}

TEST_CASE("computeSlideId separates adjacent numeric fields", "[core][SlideId]")
{
    // Without a separator, (index 0, width 1, height 12, channels 3) and
    // (index 0, width 11, height 2, channels 3) both flatten to "01123".
    const std::vector<SceneInfo> a{makeScene(0, 1, 12, 3)};
    const std::vector<SceneInfo> b{makeScene(0, 11, 2, 3)};

    REQUIRE(computeSlideId(a, 4096) != computeSlideId(b, 4096));
}

TEST_CASE("computeSlideId is identical for every scene of one file", "[core][SlideId]")
{
    // Build the SlideInfo as each scene in turn would produce it: the
    // open-scene fields differ, the scene table does not. One ID must result.
    SlideInfo asSceneZero;
    asSceneZero.scenes = twoScenes();
    asSceneZero.width = 100000;
    asSceneZero.height = 80000;
    asSceneZero.numChannels = 3;
    asSceneZero.numZoomLevels = 9;
    asSceneZero.resolutionX = 0.00000025;
    asSceneZero.driverName = "SVS";
    asSceneZero.driverId = "SVS";

    SlideInfo asSceneOne = asSceneZero;
    asSceneOne.width = 2048;
    asSceneOne.height = 1024;
    asSceneOne.numZoomLevels = 2;
    asSceneOne.resolutionX = 0.0000005;
    asSceneOne.driverId = ""; // auto-detected open of the same file

    REQUIRE(computeSlideId(asSceneZero, 4096) == computeSlideId(asSceneOne, 4096));
}

TEST_CASE("computeSlideId overloads agree", "[core][SlideId]")
{
    SlideInfo info;
    info.scenes = twoScenes();
    info.width = 100000;

    REQUIRE(computeSlideId(info, 4096) == computeSlideId(info.scenes, 4096));
}

TEST_CASE("computeSlideId tolerates an empty scene vector", "[core][SlideId]")
{
    // enumerateScenes returns empty for a file it cannot read. The hash stays
    // total; refusing the lookup is the resolver's job, not this function's.
    //
    // Spelled with an explicit type rather than `{}`: an empty braced-init-list
    // is ambiguous between the two overloads (it can construct an empty vector
    // or a default-constructed SlideInfo equally well), so the call must name
    // the vector overload's parameter type to pick one.
    const std::vector<SceneInfo> noScenes;
    const std::string id = computeSlideId(noScenes, 0);

    REQUIRE(id.size() == 16);
    REQUIRE(id == computeSlideId(noScenes, 0));
    REQUIRE(id != computeSlideId(twoScenes(), 4096));
}

TEST_CASE("computeSlideId does not collide across realistic slides", "[core][SlideId]")
{
    // Review Focus 4: pins the collision surface so it cannot silently widen.
    // Review Focus 3: a slide re-saved to a different size is a different ID,
    // which is a clean miss rather than a wrong match.
    const std::vector<std::pair<std::vector<SceneInfo>, uint64_t>> corpus{
        {{makeScene(0, 100000, 80000, 3)}, 2'400'000'000ULL},
        {{makeScene(0, 100000, 80000, 3)}, 2'400'000'001ULL}, // re-saved
        {{makeScene(0, 100000, 80000, 4)}, 2'400'000'000ULL},
        {{makeScene(0, 80000, 100000, 3)}, 2'400'000'000ULL},
        {{makeScene(0, 46000, 32914, 3)}, 1'073'741'824ULL},
        {{makeScene(0, 1024, 1024, 1), makeScene(1, 512, 512, 1)}, 10'485'760ULL},
        {{makeScene(0, 1024, 1024, 1)}, 10'485'760ULL},
        {{makeScene(0, 512, 512, 1), makeScene(1, 1024, 1024, 1)}, 10'485'760ULL},
        {{}, 0ULL},
    };

    std::set<std::string> ids;
    for (const auto& entry : corpus) {
        ids.insert(computeSlideId(entry.first, entry.second));
    }

    REQUIRE(ids.size() == corpus.size());
}
