#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "slideio/viewer/core/Viewport.h"

using namespace slideio::viewer::core;
using Catch::Matchers::WithinAbs;

TEST_CASE("Viewport initial state", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);

    REQUIRE(vp.screenWidth() == 800);
    REQUIRE(vp.screenHeight() == 600);
}

TEST_CASE("Viewport fitToSlide", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    // Slide aspect ratio 2:1, screen aspect 4:3
    // Width-limited: scale = 800/4000 = 0.2
    // Height check: 2000 * 0.2 = 400 <= 600, OK
    REQUIRE_THAT(vp.scale(), WithinAbs(0.2, 1e-9));

    // Center should be at slide center
    REQUIRE_THAT(vp.centerX(), WithinAbs(2000.0, 1e-9));
    REQUIRE_THAT(vp.centerY(), WithinAbs(1000.0, 1e-9));
}

TEST_CASE("Viewport fitToSlide tall slide", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(1000, 3000);

    // Height-limited: scale = 600/3000 = 0.2
    REQUIRE_THAT(vp.scale(), WithinAbs(0.2, 1e-9));
}

TEST_CASE("Viewport pan", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);
    double cx = vp.centerX();
    double cy = vp.centerY();

    // Pan 100 screen pixels right, 50 down
    vp.pan(100.0, 50.0);

    // pan() subtracts: dragging right moves center left in slide space
    double expectedDx = 100.0 / vp.scale();
    double expectedDy = 50.0 / vp.scale();
    REQUIRE_THAT(vp.centerX(), WithinAbs(cx - expectedDx, 1e-9));
    REQUIRE_THAT(vp.centerY(), WithinAbs(cy - expectedDy, 1e-9));
}

TEST_CASE("Viewport zoomToPoint keeps point fixed", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    // Pick a screen point
    double screenX = 200.0;
    double screenY = 150.0;

    // Convert to slide coords before zoom
    double slideX, slideY;
    vp.screenToSlide(screenX, screenY, slideX, slideY);

    // Zoom in 2x at that point
    vp.zoomToPoint(screenX, screenY, vp.scale() * 2.0);

    // The same screen point should still map to the same slide point
    double newSlideX, newSlideY;
    vp.screenToSlide(screenX, screenY, newSlideX, newSlideY);

    REQUIRE_THAT(newSlideX, WithinAbs(slideX, 0.5));
    REQUIRE_THAT(newSlideY, WithinAbs(slideY, 0.5));
}

TEST_CASE("Viewport setActualPixels", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.setActualPixels(4000, 2000);

    REQUIRE_THAT(vp.scale(), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(vp.centerX(), WithinAbs(2000.0, 1e-9));
    REQUIRE_THAT(vp.centerY(), WithinAbs(1000.0, 1e-9));
}

TEST_CASE("Viewport screenToSlide and slideToScreen roundtrip", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    double screenX = 300.0, screenY = 200.0;
    double slideX, slideY;
    vp.screenToSlide(screenX, screenY, slideX, slideY);

    double backX, backY;
    vp.slideToScreen(slideX, slideY, backX, backY);

    REQUIRE_THAT(backX, WithinAbs(screenX, 1e-6));
    REQUIRE_THAT(backY, WithinAbs(screenY, 1e-6));
}

TEST_CASE("Viewport visibleSlideRect", "[core][Viewport]")
{
    Viewport vp;
    vp.setScreenSize(800, 600);
    vp.fitToSlide(4000, 2000);

    auto rect = vp.visibleSlideRect();
    // Entire slide should be visible (or very close, might have padding)
    REQUIRE(rect.width > 0);
    REQUIRE(rect.height > 0);
}
