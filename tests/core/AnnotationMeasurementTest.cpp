#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <cmath>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::Outline closedOutline(std::vector<core::PointF> points)
{
    return core::Outline{std::move(points), true};
}

core::Outline openOutline(std::vector<core::PointF> points)
{
    return core::Outline{std::move(points), false};
}

} // namespace

TEST_CASE("a simple convex polygon does not self-intersect", "[core][AnnotationMeasurement]")
{
    REQUIRE_FALSE(core::isSelfIntersecting(
        closedOutline({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}})));
}

TEST_CASE("a simple concave polygon does not self-intersect", "[core][AnnotationMeasurement]")
{
    // An arrowhead: concave at the notch, but no edge crosses another.
    REQUIRE_FALSE(core::isSelfIntersecting(
        closedOutline({{0.0, 0.0}, {10.0, 5.0}, {0.0, 10.0}, {3.0, 5.0}})));
}

TEST_CASE("a figure-eight self-intersects", "[core][AnnotationMeasurement]")
{
    // The first and third edges cross in the middle.
    REQUIRE(core::isSelfIntersecting(
        closedOutline({{0.0, 0.0}, {10.0, 10.0}, {10.0, 0.0}, {0.0, 10.0}})));
}

TEST_CASE("a crossing created only by the closing edge is detected",
          "[core][AnnotationMeasurement]")
{
    // A simple open path: (0,0)->(10,0)->(10,10)->(20,5). Its only non-adjacent
    // pair is the first and last segments, and the last never reaches y=0.
    //
    // Closing it runs an edge from (20,5) back to (0,0), which crosses the
    // vertical segment x=10 at (10, 2.5). That pair is non-adjacent only once
    // the shape is closed, so a detector that forgets the closing edge passes
    // the open case and fails this one.
    const std::vector<core::PointF> points{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {20.0, 5.0}};
    REQUIRE(core::isSelfIntersecting(closedOutline(points)));
    REQUIRE_FALSE(core::isSelfIntersecting(openOutline(points)));
}

TEST_CASE("segments sharing an endpoint are not an intersection",
          "[core][AnnotationMeasurement]")
{
    // Every polygon's consecutive edges touch. If that counted, nothing would
    // ever be simple.
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({{0.0, 0.0}, {5.0, 0.0}, {5.0, 5.0}})));
}

TEST_CASE("collinear overlapping segments count as self-intersecting",
          "[core][AnnotationMeasurement]")
{
    // A trace that doubles back exactly along itself. Its interior is not
    // well-defined, so declining to measure it is the honest answer.
    REQUIRE(core::isSelfIntersecting(
        openOutline({{0.0, 0.0}, {10.0, 0.0}, {4.0, 0.0}, {12.0, 0.0}})));
}

TEST_CASE("a shape with too few points cannot self-intersect",
          "[core][AnnotationMeasurement]")
{
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({})));
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({{0.0, 0.0}})));
    REQUIRE_FALSE(core::isSelfIntersecting(closedOutline({{0.0, 0.0}, {5.0, 5.0}})));
}

namespace
{

/// RectangleGeometry stores two CORNERS, not an origin plus a size.
core::AnnotationGeometry rect(double x0, double y0, double x1, double y1)
{
    return core::RectangleGeometry{core::PointF{x0, y0}, core::PointF{x1, y1}};
}

core::AnnotationGeometry polygon(std::vector<core::PointF> vertices, bool closed)
{
    core::PolygonGeometry geometry;
    geometry.vertices = std::move(vertices);
    geometry.closed = closed;
    return geometry;
}

} // namespace

TEST_CASE("a rectangle measures its area and perimeter", "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m = core::measure(rect(0.0, 0.0, 10.0, 20.0), core::PixelSize{});
    REQUIRE(m.closed);
    REQUIRE_FALSE(m.selfIntersecting);
    REQUIRE(m.area.has_value());
    REQUIRE(*m.area == 200.0);
    REQUIRE(m.outlineLength == 60.0);
}

TEST_CASE("an inverted rectangle still measures positive", "[core][AnnotationMeasurement]")
{
    // Review Focus 2. A drag that went up and left puts bottomRight above
    // topLeft. outline() normalises the box, so the measurement matches the
    // shape that is actually painted rather than reporting a negative area.
    const core::ShapeMetrics m = core::measure(rect(10.0, 20.0, 0.0, 0.0), core::PixelSize{});
    REQUIRE(*m.area == 200.0);
    REQUIRE(m.outlineLength == 60.0);
}

TEST_CASE("anisotropic pixels scale area by the product", "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m = core::measure(rect(0.0, 0.0, 10.0, 20.0), core::PixelSize{2.0, 3.0});
    REQUIRE(*m.area == 1200.0);        // (10*2) * (20*3)
    REQUIRE(m.outlineLength == 160.0); // 2 * (20 + 60)
}

TEST_CASE("a diagonal under anisotropic pixels is not scaled by one factor",
          "[core][AnnotationMeasurement]")
{
    // THE test for the API's shape. A 45-degree segment from (0,0) to (10,10)
    // under PixelSize{1,2} becomes (0,0)->(10,20), whose length is
    // sqrt(100 + 400) = 22.360..., which is neither 14.142 (unscaled) nor
    // 28.284 (scaled by 2). Any implementation that measures in pixels and
    // multiplies afterwards gets this wrong.
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 10.0}}, false), core::PixelSize{1.0, 2.0});
    REQUIRE_FALSE(m.closed);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE(std::abs(m.outlineLength - std::sqrt(500.0)) < 1e-9);
}

TEST_CASE("a closed triangle measures by the shoelace formula",
          "[core][AnnotationMeasurement]")
{
    // Base 10, height 10, so area 50. Perimeter 10 + sqrt(200) + 10.
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}}, true), core::PixelSize{});
    REQUIRE(*m.area == 50.0);
    REQUIRE(std::abs(m.outlineLength - (10.0 + std::sqrt(200.0) + 10.0)) < 1e-9);
}

TEST_CASE("an open polyline has no area and excludes the closing edge",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}}, false), core::PixelSize{});
    REQUIRE_FALSE(m.closed);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE(m.outlineLength == 20.0); // not 20 + sqrt(200)
}

TEST_CASE("a self-intersecting polygon reports no area but a real perimeter",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 10.0}, {10.0, 0.0}, {0.0, 10.0}}, true),
                      core::PixelSize{});
    REQUIRE(m.selfIntersecting);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE(m.outlineLength > 0.0);
}

TEST_CASE("an open trace is never tested for self-intersection",
          "[core][AnnotationMeasurement]")
{
    // The same crossing points, left open. Area is absent either way, so the
    // O(n^2) test is skipped -- and open freehand traces are exactly the shapes
    // carrying thousands of vertices. selfIntersecting therefore means "this is
    // why the area is missing", not "this shape is geometrically simple".
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 10.0}, {10.0, 0.0}, {0.0, 10.0}}, false),
                      core::PixelSize{});
    REQUIRE_FALSE(m.selfIntersecting);
    REQUIRE_FALSE(m.area.has_value());
}

TEST_CASE("a degenerate shape measures zero, not NaN", "[core][AnnotationMeasurement]")
{
    // Review Focus 1. A stray two-pixel drag, or a trace whose points coincide.
    const core::ShapeMetrics zeroRect = core::measure(rect(5.0, 5.0, 5.0, 5.0), core::PixelSize{});
    REQUIRE(*zeroRect.area == 0.0);
    REQUIRE(zeroRect.outlineLength == 0.0);

    const core::ShapeMetrics coincident =
        core::measure(polygon({{3.0, 3.0}, {3.0, 3.0}, {3.0, 3.0}}, true), core::PixelSize{});
    REQUIRE(coincident.outlineLength == 0.0);
    REQUIRE(std::isfinite(coincident.outlineLength));
    if (coincident.area.has_value()) {
        REQUIRE(*coincident.area == 0.0);
    }
}

TEST_CASE("a non-positive pixel size yields zero, not NaN or infinity",
          "[core][AnnotationMeasurement]")
{
    // Review Focus 3. resolutionX is 0 on an uncalibrated slide and a caller
    // could pass it straight through. Zero is a defensible answer; NaN is not.
    const core::ShapeMetrics m = core::measure(rect(0.0, 0.0, 10.0, 20.0), core::PixelSize{0.0, 0.0});
    REQUIRE(m.area.has_value());
    REQUIRE(*m.area == 0.0);
    REQUIRE(m.outlineLength == 0.0);
    REQUIRE(std::isfinite(*m.area));
}

TEST_CASE("measureKnown trusts the supplied self-intersection answer",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry simple = polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}}, true);

    // Told it is simple: the area is computed.
    const core::ShapeMetrics trusted = core::measureKnown(simple, core::PixelSize{}, false);
    REQUIRE(trusted.area.has_value());
    REQUIRE(*trusted.area == 50.0);

    // Told it self-intersects: the area is withheld even though it does not.
    // That is the contract, and why only a caller that can prove the outline is
    // unchanged may use it. The length is unaffected either way.
    const core::ShapeMetrics withheld = core::measureKnown(simple, core::PixelSize{}, true);
    REQUIRE_FALSE(withheld.area.has_value());
    REQUIRE(withheld.selfIntersecting);
    REQUIRE(withheld.outlineLength == trusted.outlineLength);
}

TEST_CASE("measureKnown ignores the flag for an open path", "[core][AnnotationMeasurement]")
{
    // An open path has no area because it is open, not because it crosses
    // itself, so the flag must not leak into selfIntersecting and imply a
    // reason that is not the real one.
    const core::ShapeMetrics m =
        core::measureKnown(polygon({{0.0, 0.0}, {10.0, 0.0}}, false), core::PixelSize{}, true);
    REQUIRE_FALSE(m.area.has_value());
    REQUIRE_FALSE(m.selfIntersecting);
}
