#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/VertexEditing.h"

#include <cmath>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

// A unit square, closed. Vertices at (0,0) (10,0) (10,10) (0,10).
PolygonGeometry square()
{
    return PolygonGeometry{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{10.0, 10.0},
                            PointF{0.0, 10.0}},
                           true};
}

} // namespace

TEST_CASE("closestPointOnSegment projects onto the segment", "[core][VertexEditing]")
{
    const PointF p = closestPointOnSegment(PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{3.0, 5.0});
    REQUIRE(p.x == 3.0);
    REQUIRE(p.y == 0.0);
}

// Past either end it clamps to the endpoint rather than running off the line.
TEST_CASE("closestPointOnSegment clamps beyond the ends", "[core][VertexEditing]")
{
    const PointF before = closestPointOnSegment(PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                                PointF{-5.0, 1.0});
    REQUIRE(before.x == 0.0);
    const PointF after = closestPointOnSegment(PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                               PointF{15.0, 1.0});
    REQUIRE(after.x == 10.0);
}

TEST_CASE("closestPointOnSegment on a zero-length segment is its endpoint",
          "[core][VertexEditing]")
{
    const PointF p = closestPointOnSegment(PointF{4.0, 4.0}, PointF{4.0, 4.0}, PointF{9.0, 9.0});
    REQUIRE(p.x == 4.0);
    REQUIRE(p.y == 4.0);
}

TEST_CASE("nearestVertex finds a vertex in tolerance", "[core][VertexEditing]")
{
    const auto index = nearestVertex(square(), PointF{10.0, 10.4}, 1.0);
    REQUIRE(index.has_value());
    REQUIRE(*index == 2);
}

TEST_CASE("nearestVertex is empty when nothing is in range", "[core][VertexEditing]")
{
    REQUIRE_FALSE(nearestVertex(square(), PointF{5.0, 5.0}, 1.0).has_value());
}

// Review Focus 1 and design spec 5.1. Two vertices inside the tolerance: the
// nearer one wins, whichever order they appear in.
TEST_CASE("nearestVertex returns the nearest of several in range", "[core][VertexEditing]")
{
    const PolygonGeometry dense{
        {PointF{0.0, 0.0}, PointF{1.0, 0.0}, PointF{2.0, 0.0}, PointF{3.0, 0.0}}, true};
    const auto index = nearestVertex(dense, PointF{2.1, 0.0}, 5.0);
    REQUIRE(index.has_value());
    REQUIRE(*index == 2);
}

// Review Focus 1. A duplicate vertex is a STORED vertex and must be
// addressable; outline() would have dropped it and shifted every later index.
TEST_CASE("nearestVertex addresses stored indices, duplicates included",
          "[core][VertexEditing]")
{
    const PolygonGeometry withDuplicate{
        {PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{10.0, 10.0}, PointF{10.0, 10.0}}, true};
    REQUIRE(withDuplicate.vertices.size() == 4);
    const auto index = nearestVertex(withDuplicate, PointF{0.0, 0.1}, 1.0);
    REQUIRE(index.has_value());
    REQUIRE(*index == 0);
}

TEST_CASE("a negative tolerance behaves as zero", "[core][VertexEditing]")
{
    REQUIRE(nearestVertex(square(), PointF{0.0, 0.0}, -5.0).has_value());
    REQUIRE_FALSE(nearestVertex(square(), PointF{0.5, 0.0}, -5.0).has_value());
}

TEST_CASE("nearestEdge finds the segment and the projected point",
          "[core][VertexEditing]")
{
    const auto hit = nearestEdge(square(), PointF{5.0, 0.3}, 1.0);
    REQUIRE(hit.has_value());
    REQUIRE(hit->afterIndex == 0);
    REQUIRE(hit->position.x == 5.0);
    REQUIRE(hit->position.y == 0.0);
}

// Review Focus 5. The closing segment of a closed shape runs from the last
// vertex back to the first and is a real edge the user can insert on.
TEST_CASE("nearestEdge includes the closing segment of a closed shape",
          "[core][VertexEditing]")
{
    const auto hit = nearestEdge(square(), PointF{0.3, 5.0}, 1.0);
    REQUIRE(hit.has_value());
    REQUIRE(hit->afterIndex == 3);
}

TEST_CASE("nearestEdge excludes the closing segment of an open path",
          "[core][VertexEditing]")
{
    PolygonGeometry open = square();
    open.closed = false;
    REQUIRE_FALSE(nearestEdge(open, PointF{0.3, 5.0}, 1.0).has_value());
}

TEST_CASE("nearestEdge is empty off the outline", "[core][VertexEditing]")
{
    REQUIRE_FALSE(nearestEdge(square(), PointF{5.0, 5.0}, 1.0).has_value());
}

// Review Focus 3. A hand-edited file can hold these; neither function may
// index out of range or invent a segment.
TEST_CASE("degenerate polygons answer without indexing out of range",
          "[core][VertexEditing]")
{
    const PolygonGeometry empty{{}, true};
    REQUIRE_FALSE(nearestVertex(empty, PointF{0.0, 0.0}, 5.0).has_value());
    REQUIRE_FALSE(nearestEdge(empty, PointF{0.0, 0.0}, 5.0).has_value());

    const PolygonGeometry single{{PointF{1.0, 1.0}}, true};
    REQUIRE(nearestVertex(single, PointF{1.0, 1.0}, 5.0).has_value());
    REQUIRE_FALSE(nearestEdge(single, PointF{1.0, 1.0}, 5.0).has_value());
}

// Review Focus 2. A press on a handle is in range of the vertex AND of both
// segments meeting there. The caller asks nearestVertex first (design spec
// 3.4); this pins that it has an answer to give.
TEST_CASE("a press on a corner is in range of both a vertex and an edge",
          "[core][VertexEditing]")
{
    REQUIRE(nearestVertex(square(), PointF{10.0, 0.0}, 1.0).has_value());
    REQUIRE(nearestEdge(square(), PointF{10.0, 0.0}, 1.0).has_value());
}
