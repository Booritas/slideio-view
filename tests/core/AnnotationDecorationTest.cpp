#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationDecoration.h"

#include <cmath>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

constexpr double kRadius = 6.0;

} // namespace

// Review Focus 5: a glyph on a shape that has none would be a dot or a
// triangle drawn on a rectangle. The painter asks this question about every
// annotation it draws, so every other type has to say no.
TEST_CASE("the three existing shapes need no decoration",
          "[core][AnnotationDecoration]")
{
    REQUIRE_FALSE(screenDecoration(
        RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}}, 1.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(
        EllipseGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}}, 1.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(
        PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{0.0, 10.0}}, true},
        1.0, kRadius).has_value());
}

TEST_CASE("a point decorates as a disc at its position",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph =
        screenDecoration(PointGeometry{PointF{120.0, 340.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->kind == ScreenDecoration::Kind::Disc);
    REQUIRE(glyph->anchor.x == 120.0);
    REQUIRE(glyph->anchor.y == 340.0);
    REQUIRE(glyph->sizePixels == kRadius);
}

// The radius is returned as given, at every zoom. That is what makes it the
// caller's number and lets ViewportWidget pass the same value it passes to the
// hit test -- so the marker you see is the target you click.
TEST_CASE("a disc keeps the radius it was given at any scale",
          "[core][AnnotationDecoration]")
{
    for (const double scale : std::vector<double>{0.01, 1.0, 40.0}) {
        const std::optional<ScreenDecoration> glyph =
            screenDecoration(PointGeometry{PointF{1.0, 2.0}}, scale, kRadius);
        REQUIRE(glyph.has_value());
        REQUIRE(glyph->sizePixels == kRadius);
    }
}

TEST_CASE("an arrow decorates as a head at its head point",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{100.0, 0.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->kind == ScreenDecoration::Kind::ArrowHead);
    REQUIRE(glyph->anchor.x == 100.0);
    REQUIRE(glyph->anchor.y == 0.0);
}

// A long shaft: the fixed size is the smaller of the two and wins, so the head
// does not grow without limit on a long arrow.
TEST_CASE("a long shaft takes the fixed head size",
          "[core][AnnotationDecoration]")
{
    // 100 slide units at scale 1 is 100 screen pixels; half of that is 50,
    // which is larger than kArrowHeadScreenPixels.
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{100.0, 0.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == kArrowHeadScreenPixels);
}

// A short shaft -- or a long one zoomed out, which is the same thing on screen
// -- takes the fraction, so the head never swallows its own arrow.
TEST_CASE("a short shaft takes the fraction of its length",
          "[core][AnnotationDecoration]")
{
    // 10 slide units at scale 1 is 10 screen pixels; half is 5, smaller than
    // kArrowHeadScreenPixels.
    const std::optional<ScreenDecoration> shortArrow = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{10.0, 0.0}}, 1.0, kRadius);
    REQUIRE(shortArrow.has_value());
    REQUIRE(shortArrow->sizePixels == 10.0 * kArrowHeadMaxShaftFraction);

    // The same long arrow as above, zoomed out until its shaft is 10 pixels.
    const std::optional<ScreenDecoration> zoomedOut = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{100.0, 0.0}}, 0.1, kRadius);
    REQUIRE(zoomedOut.has_value());
    REQUIRE(zoomedOut->sizePixels == 10.0 * kArrowHeadMaxShaftFraction);
}

// The shaft is measured as a hypotenuse, not per axis: a 3-4-5 diagonal is 50
// screen pixels long at scale 10, so the fixed size wins.
TEST_CASE("a diagonal shaft is measured along its length",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{3.0, 4.0}}, 10.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == kArrowHeadScreenPixels);

    // The same diagonal at scale 1 is 5 pixels long: the fraction wins.
    const std::optional<ScreenDecoration> small = screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{3.0, 4.0}}, 1.0, kRadius);
    REQUIRE(small.has_value());
    REQUIRE(small->sizePixels == 5.0 * kArrowHeadMaxShaftFraction);
}

// Review Focus 1: the tool refuses to create this, but the parser accepts it
// from disk. The answer is a head of size 0, not a NaN the painter would then
// turn into an undrawable polygon.
TEST_CASE("a zero-length arrow gets a head of size zero",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph = screenDecoration(
        ArrowGeometry{PointF{40.0, 40.0}, PointF{40.0, 40.0}}, 1.0, kRadius);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == 0.0);
    REQUIRE(std::isfinite(glyph->sizePixels));
}

// No usable transform, no screen size. Returning something would mean dividing
// by -- or multiplying through -- a scale that means nothing.
TEST_CASE("a non-positive scale yields no decoration",
          "[core][AnnotationDecoration]")
{
    REQUIRE_FALSE(screenDecoration(PointGeometry{PointF{1.0, 2.0}}, 0.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(PointGeometry{PointF{1.0, 2.0}}, -1.0, kRadius).has_value());
    REQUIRE_FALSE(screenDecoration(
        ArrowGeometry{PointF{0.0, 0.0}, PointF{10.0, 0.0}}, 0.0, kRadius).has_value());
}

// A caller that hands over a negative radius gets zero, not a disc drawn
// inside out.
TEST_CASE("a negative disc radius clamps to zero",
          "[core][AnnotationDecoration]")
{
    const std::optional<ScreenDecoration> glyph =
        screenDecoration(PointGeometry{PointF{1.0, 2.0}}, 1.0, -4.0);
    REQUIRE(glyph.has_value());
    REQUIRE(glyph->sizePixels == 0.0);
}
