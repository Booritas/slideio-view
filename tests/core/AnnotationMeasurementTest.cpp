#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationMeasurement.h"

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
