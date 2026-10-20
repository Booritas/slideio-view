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

TEST_CASE("fitToRect centres on the target and fits it", "[core][Viewport]")
{
    Viewport viewport(0.0, 0.0, 1.0, 800, 600);
    viewport.fitToRect(RectF{100.0, 200.0, 400.0, 300.0}, 0.1);

    REQUIRE_THAT(viewport.centerX(), WithinAbs(300.0, 1e-9));
    REQUIRE_THAT(viewport.centerY(), WithinAbs(350.0, 1e-9));

    // 400 wide into 800 screen px with 10% margin on each side: 640/400 = 1.6.
    // 300 tall into 600 screen px likewise: 480/300 = 1.6. Both agree here.
    REQUIRE_THAT(viewport.scale(), WithinAbs(1.6, 1e-9));
}

TEST_CASE("fitToRect fits the tighter axis", "[core][Viewport]")
{
    Viewport viewport(0.0, 0.0, 1.0, 800, 600);
    // Wide and short: width is the binding constraint.
    viewport.fitToRect(RectF{0.0, 0.0, 800.0, 100.0}, 0.0);
    REQUIRE_THAT(viewport.scale(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("fitToRect survives a degenerate box", "[core][Viewport]")
{
    // Review Focus 1. A perfectly horizontal freehand line has zero height, and
    // a vertical one zero width. Fitting the zero axis would divide by it; the
    // annotation still has to end up centred and visible rather than at an
    // infinite or NaN scale.
    Viewport viewport(0.0, 0.0, 1.0, 800, 600);
    viewport.fitToRect(RectF{100.0, 50.0, 400.0, 0.0}, 0.1);

    REQUIRE_THAT(viewport.centerX(), WithinAbs(300.0, 1e-9));
    REQUIRE_THAT(viewport.centerY(), WithinAbs(50.0, 1e-9));
    REQUIRE_THAT(viewport.scale(), WithinAbs(1.6, 1e-9));
    REQUIRE(std::isfinite(viewport.scale()));

    // Both axes degenerate -- a point annotation, or a trace whose vertices all
    // coincide. There is no meaningful zoom, so the previous one is kept and
    // only the centre moves.
    Viewport point(0.0, 0.0, 2.5, 800, 600);
    point.fitToRect(RectF{10.0, 20.0, 0.0, 0.0}, 0.1);
    REQUIRE_THAT(point.centerX(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(point.centerY(), WithinAbs(20.0, 1e-9));
    REQUIRE_THAT(point.scale(), WithinAbs(2.5, 1e-9));
}

TEST_CASE("fitToRect ignores a screen with no area", "[core][Viewport]")
{
    // Matches fitToSlide's guard: a widget that has not been laid out yet.
    Viewport viewport(5.0, 6.0, 1.0, 0, 0);
    viewport.fitToRect(RectF{100.0, 200.0, 400.0, 300.0}, 0.1);
    REQUIRE_THAT(viewport.centerX(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(viewport.centerY(), WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(viewport.scale(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("fitToRect clamps the margin to 0.45", "[core][Viewport]")
{
    // At 0.5 the usable width is zero and the scale would be infinite, so the
    // clamp is what stops a caller asking for a margin that consumes the whole
    // screen. Asserting it through two margins that must agree: anything at or
    // above 0.45 is the same fit.
    Viewport clamped(0.0, 0.0, 1.0, 800, 600);
    clamped.fitToRect(RectF{0.0, 0.0, 400.0, 300.0}, 0.45);

    Viewport overAsked(0.0, 0.0, 1.0, 800, 600);
    overAsked.fitToRect(RectF{0.0, 0.0, 400.0, 300.0}, 5.0);

    REQUIRE(overAsked.scale() == clamped.scale());
    REQUIRE(std::isfinite(overAsked.scale()));
    REQUIRE(overAsked.scale() > 0.0);
}

TEST_CASE("fitToRect clamps a negative margin to zero", "[core][Viewport]")
{
    Viewport noMargin(0.0, 0.0, 1.0, 800, 600);
    noMargin.fitToRect(RectF{0.0, 0.0, 400.0, 300.0}, 0.0);

    Viewport negative(0.0, 0.0, 1.0, 800, 600);
    negative.fitToRect(RectF{0.0, 0.0, 400.0, 300.0}, -1.0);

    REQUIRE(negative.scale() == noMargin.scale());
}
