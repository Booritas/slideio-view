#include "slideio/viewer/ui/OverviewRendering.h"

#include "slideio/viewer/core/DisplayRangePolicy.h"

#include <catch2/catch_test_macros.hpp>

using namespace slideio::viewer;
using core::ChannelInfo;
using core::DataType;
using core::SlideInfo;
using core::TileData;

namespace
{

ChannelInfo channel(float r, float g, float b)
{
    ChannelInfo c;
    c.dataType = DataType::Byte;
    c.colorR = r;
    c.colorG = g;
    c.colorB = b;
    return c;
}

SlideInfo slide(int numChannels, bool brightfield)
{
    SlideInfo s;
    s.numChannels = numChannels;
    s.channelDataType = DataType::Byte;
    s.isBrightfield = brightfield;
    s.displayRange.displayMin = 0.0;
    s.displayRange.displayMax = 255.0;
    return s;
}

} // namespace

TEST_CASE("brightfield RGB at native range renders its own colours", "[ui][OverviewRendering]")
{
    SlideInfo info = slide(3, true);
    info.channels = {channel(1, 0, 0), channel(0, 1, 0), channel(0, 0, 1)};
    core::applyNativeDisplayRange(info);

    // Two pixels: (200, 100, 50) and (10, 20, 30).
    TileData block({200, 100, 50, 10, 20, 30}, 2, 1, 3, DataType::Byte);
    const QImage img = ui::renderOverviewImage(block, info);

    REQUIRE(img.format() == QImage::Format_RGB888);
    REQUIRE(img.width() == 2);
    CHECK(img.pixelColor(0, 0).red() == 200);
    CHECK(img.pixelColor(0, 0).green() == 100);
    CHECK(img.pixelColor(0, 0).blue() == 50);
    CHECK(img.pixelColor(1, 0).red() == 10);
    CHECK(img.pixelColor(1, 0).green() == 20);
    CHECK(img.pixelColor(1, 0).blue() == 30);
}

TEST_CASE("BGR-labelled channels are not swapped", "[ui][OverviewRendering]")
{
    // SlideIO labels a CZI's B, G, R planes with the colours blue, green, red.
    SlideInfo info = slide(3, true);
    info.channels = {channel(0, 0, 1), channel(0, 1, 0), channel(1, 0, 0)};
    core::applyNativeDisplayRange(info);

    // Plane 0 = 30 (blue), plane 1 = 20 (green), plane 2 = 10 (red).
    TileData block({30, 20, 10}, 1, 1, 3, DataType::Byte);
    const QImage img = ui::renderOverviewImage(block, info);

    REQUIRE(!img.isNull());
    CHECK(img.pixelColor(0, 0).red() == 10);
    CHECK(img.pixelColor(0, 0).green() == 20);
    CHECK(img.pixelColor(0, 0).blue() == 30);
}

TEST_CASE("single-channel brightfield renders grayscale through the display range", "[ui][OverviewRendering]")
{
    SlideInfo info = slide(1, true);
    info.displayRange.displayMin = 100.0;
    info.displayRange.displayMax = 200.0;

    // 100 -> 0, 150 -> 128 (127.5 rounds up), 200 -> 255, 250 clamps to 255, 50 clamps to 0.
    TileData block({100, 150, 200, 250, 50}, 5, 1, 1, DataType::Byte);
    const QImage img = ui::renderOverviewImage(block, info);

    REQUIRE(img.format() == QImage::Format_Grayscale8);
    const uint8_t* row = img.constScanLine(0);
    CHECK(row[0] == 0);
    CHECK(row[1] == 128);
    CHECK(row[2] == 255);
    CHECK(row[3] == 255);
    CHECK(row[4] == 0);
}

TEST_CASE("a hidden channel contributes nothing", "[ui][OverviewRendering]")
{
    SlideInfo info = slide(3, false);
    info.channels = {channel(1, 0, 0), channel(0, 1, 0), channel(0, 0, 1)};
    info.channels[1].visible = false;

    TileData block({255, 255, 255}, 1, 1, 3, DataType::Byte);
    const QImage img = ui::renderOverviewImage(block, info);

    REQUIRE(!img.isNull());
    CHECK(img.pixelColor(0, 0).red() == 255);
    CHECK(img.pixelColor(0, 0).green() == 0);
    CHECK(img.pixelColor(0, 0).blue() == 255);
}

TEST_CASE("an empty or error block renders a null image", "[ui][OverviewRendering]")
{
    const SlideInfo info = slide(3, true);
    CHECK(ui::renderOverviewImage(TileData(), info).isNull());
    CHECK(ui::renderOverviewImage(TileData::createError(4, 4), info).isNull());
}

TEST_CASE("a built overview is published only when the open produced none", "[ui][OverviewRendering]")
{
    using ui::shouldPublishBuiltOverview;
    CHECK(shouldPublishBuiltOverview(false, false));
    // An open-time thumbnail is never replaced by the built one.
    CHECK_FALSE(shouldPublishBuiltOverview(true, false));
    // Nothing to publish: withheld (failed rows) or never built.
    CHECK_FALSE(shouldPublishBuiltOverview(false, true));
    CHECK_FALSE(shouldPublishBuiltOverview(true, true));
}

TEST_CASE("the minimap says building only when the built overview is the only one coming",
          "[ui][OverviewRendering]")
{
    using ui::shouldShowOverviewBuilding;
    CHECK(shouldShowOverviewBuilding(true, false));
    CHECK_FALSE(shouldShowOverviewBuilding(true, true));
    CHECK_FALSE(shouldShowOverviewBuilding(false, false));
    CHECK_FALSE(shouldShowOverviewBuilding(false, true));
}
