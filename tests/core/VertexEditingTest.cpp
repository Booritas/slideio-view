#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/VertexEditing.h"

#include <cmath>
#include <variant>
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
// The duplicate sits in the MIDDLE, and the press targets a vertex AFTER it.
// That is what makes this able to fail: core::outline() drops the duplicate,
// so an outline-based implementation would shift every later index down and
// answer 2 where the stored index is 3. A trailing duplicate would not
// distinguish the two, because dropping it leaves earlier indices untouched.
TEST_CASE("nearestVertex addresses stored indices, duplicates included",
          "[core][VertexEditing]")
{
    const PolygonGeometry withDuplicate{{PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                         PointF{10.0, 0.0}, PointF{10.0, 10.0}},
                                        true};
    REQUIRE(withDuplicate.vertices.size() == 4);

    const auto index = nearestVertex(withDuplicate, PointF{10.0, 9.9}, 1.0);
    REQUIRE(index.has_value());
    REQUIRE(*index == 3);
}

// The divergence the rule above exists because of, pinned directly: these two
// views of one shape have different lengths, so an index into one does not
// address the other. If outline()'s dedup is ever removed this fails, and the
// reason for every "stored vertices, never outline()" comment disappears with
// it.
TEST_CASE("outline drops a duplicate that the stored vertices keep",
          "[core][VertexEditing]")
{
    const PolygonGeometry withDuplicate{{PointF{0.0, 0.0}, PointF{10.0, 0.0},
                                         PointF{10.0, 0.0}, PointF{10.0, 10.0}},
                                        true};
    const Outline shape = outline(AnnotationGeometry{withDuplicate});

    REQUIRE(withDuplicate.vertices.size() == 4);
    REQUIRE(shape.points.size() == 3);
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

TEST_CASE("withVertexMoved moves the addressed vertex and no other",
          "[core][VertexEditing]")
{
    const PolygonGeometry moved = withVertexMoved(square(), 1, PointF{99.0, 98.0});
    REQUIRE(moved.vertices.size() == 4);
    REQUIRE(moved.vertices[0].x == 0.0);
    REQUIRE(moved.vertices[1].x == 99.0);
    REQUIRE(moved.vertices[1].y == 98.0);
    REQUIRE(moved.vertices[2].x == 10.0);
    REQUIRE(moved.closed);
}

TEST_CASE("withVertexMoved leaves an out-of-range index alone",
          "[core][VertexEditing]")
{
    const PolygonGeometry moved = withVertexMoved(square(), 99, PointF{1.0, 1.0});
    REQUIRE(moved.vertices.size() == 4);
    REQUIRE(moved.vertices[0].x == 0.0);
}

TEST_CASE("withVertexInserted puts the new vertex after the given index",
          "[core][VertexEditing]")
{
    const PolygonGeometry grown = withVertexInserted(square(), 0, PointF{5.0, 0.0});
    REQUIRE(grown.vertices.size() == 5);
    REQUIRE(grown.vertices[1].x == 5.0);
    REQUIRE(grown.vertices[1].y == 0.0);
    REQUIRE(grown.vertices[2].x == 10.0);
    REQUIRE(grown.closed);
}

// Review Focus 5. Inserting on the closing segment means appending: the new
// vertex sits between the old last and the first, which is exactly that edge.
TEST_CASE("withVertexInserted on the closing segment appends",
          "[core][VertexEditing]")
{
    const PolygonGeometry grown = withVertexInserted(square(), 3, PointF{0.0, 5.0});
    REQUIRE(grown.vertices.size() == 5);
    REQUIRE(grown.vertices[4].x == 0.0);
    REQUIRE(grown.vertices[4].y == 5.0);
}

TEST_CASE("withVertexRemoved removes the addressed vertex", "[core][VertexEditing]")
{
    const auto shrunk = withVertexRemoved(square(), 1);
    REQUIRE(shrunk.has_value());
    REQUIRE(shrunk->vertices.size() == 3);
    REQUIRE(shrunk->vertices[1].x == 10.0);
    REQUIRE(shrunk->vertices[1].y == 10.0);
    REQUIRE(shrunk->closed);
}

// Review Focus 4. The floor is the serializer's own minimum, so the tool
// cannot build a shape the file format would refuse on write or read.
TEST_CASE("withVertexRemoved refuses at the closed-shape floor",
          "[core][VertexEditing]")
{
    const PolygonGeometry triangle{
        {PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 10.0}}, true};
    REQUIRE(triangle.vertices.size() == kMinPolygonVertices);
    REQUIRE_FALSE(withVertexRemoved(triangle, 0).has_value());
}

TEST_CASE("withVertexRemoved refuses at the open-path floor", "[core][VertexEditing]")
{
    const PolygonGeometry line{{PointF{0.0, 0.0}, PointF{10.0, 0.0}}, false};
    REQUIRE(line.vertices.size() == kMinPolylineVertices);
    REQUIRE_FALSE(withVertexRemoved(line, 0).has_value());
}

// One above the floor still removes, which is what makes the refusal a floor
// rather than a blanket refusal.
TEST_CASE("withVertexRemoved allows the removal that reaches the floor",
          "[core][VertexEditing]")
{
    const auto shrunk = withVertexRemoved(square(), 0);
    REQUIRE(shrunk.has_value());
    REQUIRE(shrunk->vertices.size() == kMinPolygonVertices);
}

TEST_CASE("withVertexRemoved refuses an out-of-range index", "[core][VertexEditing]")
{
    REQUIRE_FALSE(withVertexRemoved(square(), 99).has_value());
}

// An open path has a lower floor than a closed one, so the same vertex count
// is removable in one and not the other.
TEST_CASE("the floor depends on whether the shape is closed",
          "[core][VertexEditing]")
{
    PolygonGeometry three{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 10.0}}, true};
    REQUIRE_FALSE(withVertexRemoved(three, 0).has_value());
    three.closed = false;
    REQUIRE(withVertexRemoved(three, 0).has_value());
}

TEST_CASE("sameVertices sees an identical shape as unchanged",
          "[core][VertexEditing]")
{
    REQUIRE(sameVertices(square(), square()));
}

// The case ui::moveEdit's bounding-box comparison would get WRONG: an
// interior vertex moves, the outer extent does not budge, and the edit is
// real. Recording nothing here would lose the user's work from the undo stack.
TEST_CASE("sameVertices sees an interior move that preserves the bounding box",
          "[core][VertexEditing]")
{
    const PolygonGeometry before{{PointF{0.0, 0.0}, PointF{10.0, 0.0}, PointF{5.0, 4.0},
                                  PointF{10.0, 10.0}, PointF{0.0, 10.0}},
                                 true};
    const PolygonGeometry after = withVertexMoved(before, 2, PointF{6.0, 5.0});

    const RectF boxBefore = boundingBox(before);
    const RectF boxAfter = boundingBox(after);
    REQUIRE(boxBefore.x == boxAfter.x);
    REQUIRE(boxBefore.y == boxAfter.y);
    REQUIRE(boxBefore.width == boxAfter.width);
    REQUIRE(boxBefore.height == boxAfter.height);

    REQUIRE_FALSE(sameVertices(before, after));
}

TEST_CASE("sameVertices distinguishes count, order and closedness",
          "[core][VertexEditing]")
{
    REQUIRE_FALSE(sameVertices(square(), withVertexRemoved(square(), 0).value()));
    REQUIRE_FALSE(sameVertices(square(), withVertexInserted(square(), 0, PointF{5.0, 0.0})));

    PolygonGeometry open = square();
    open.closed = false;
    REQUIRE_FALSE(sameVertices(square(), open));
}

// Spec 7.1: "Each operation's result round-trips through the serializer
// unchanged." The operations can only build shapes the format accepts -- the
// removal floor is the serializer's own minimum -- and this is what holds them
// to it. Without it the tool could learn to produce a shape the application
// then refuses to reopen, which is the invariant the whole annotation
// subsystem is arranged around.
TEST_CASE("every vertex operation's result round-trips through the serializer",
          "[core][VertexEditing]")
{
    const PolygonGeometry start = square();

    const std::vector<PolygonGeometry> results{
        withVertexMoved(start, 1, PointF{99.0, 98.0}),
        withVertexInserted(start, 0, PointF{5.0, 0.0}),
        withVertexInserted(start, 3, PointF{0.0, 5.0}),
        withVertexRemoved(start, 0).value(),
    };

    for (const PolygonGeometry& result : results) {
        AnnotationDocument document;
        document.slideId = "a3f8c2e1b4d50697";
        document.sceneIndex = 0;
        document.annotations.emplace_back("v1", result);

        const ParseResult parsed = parseAnnotationDocument(serializeAnnotationDocument(document));
        REQUIRE(parsed.error == ParseError::None);
        REQUIRE(parsed.document.annotations.size() == 1);

        const auto* readBack =
            std::get_if<PolygonGeometry>(&parsed.document.annotations.front().geometry());
        REQUIRE(readBack != nullptr);
        REQUIRE(sameVertices(result, *readBack));
    }
}
