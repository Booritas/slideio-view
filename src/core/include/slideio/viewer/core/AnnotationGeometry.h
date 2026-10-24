#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <variant>
#include <vector>

namespace slideio::viewer::core
{

struct RectangleGeometry
{
    PointF topLeft;
    PointF bottomRight;
};

/// An axis-aligned ellipse, stored as the bounding box of the drag that made
/// it (FR-ANN-02).
///
/// The box rather than a centre and radii, so RectangleGeometry's
/// normalisation of an inverted drag, ui::dragExceedsMinimumSize and
/// ui::withMinimumExtent all apply unchanged rather than through conversions --
/// and so the written file reads the same way a rectangle's does.
struct EllipseGeometry
{
    PointF topLeft;
    PointF bottomRight;
};

/// A single marked position (FR-ANN-05). Not a zero-extent rectangle: a box
/// promises two corners that could differ, and normalisation, withBoxReplaced,
/// withMinimumExtent and the resize handles would all apply to it, each
/// meaninglessly. A position is what a marker is.
///
/// It has no slide-space extent at all, so what the user sees is sized in
/// screen pixels by core::screenDecoration rather than by outline().
struct PointGeometry
{
    PointF position;
};

/// A directed arrow from `tail` to `head` (FR-ANN-06).
///
/// Not a two-vertex PolygonGeometry: AnnotationType is derived from the
/// geometry and never stored beside it, so typeOf() must be able to tell an
/// arrow from a two-point freehand line by its geometry alone, and those two
/// would be indistinguishable.
///
/// The pair is ORDERED, and the order is the meaning -- the head is the end
/// that carries the glyph. Never normalise it the way a rectangle's corners
/// are normalised: that silently reverses arrows. boundingBox() normalises the
/// BOX it derives, which is a different thing and is fine.
struct ArrowGeometry
{
    PointF tail;
    PointF head;
};

/// Words written on the slide (FR-ANN-10), anchored at a point in slide
/// coordinates and drawn at a fixed screen size. The words themselves are the
/// annotation's label, so the list, the properties panel, the file and every
/// export already carry them; this holds only where they are.
///
/// A separate alternative rather than a PointGeometry with a flag: typeOf()
/// derives the type from the alternative, so a text and a marker sharing one
/// would be indistinguishable. Like a point it has no slide-space extent; the
/// ui sizes and hit-tests its words in screen pixels.
struct TextGeometry
{
    PointF anchor;
};

/// Named kind of a shape. Derived from the geometry rather than stored
/// alongside it, so the two cannot disagree -- see typeOf() below.
/// A distance measured on the slide (FR-ANN-07): two points, its length drawn
/// beside it. Its own alternative, not a two-point polyline or arrow, because
/// typeOf() derives the type from the alternative. The points are kept as
/// drawn and never normalised -- the direction changes no length, but it is
/// what the user drew and what the GeoJSON writes.
struct RulerGeometry
{
    PointF start;
    PointF end;
};

/// An angle measured on the slide (FR-ANN-09): two rays from `vertex`, to
/// `first` and to `second`, in the order drawn. Its outline is the open path
/// first -> vertex -> second.
struct AngleGeometry
{
    PointF first;
    PointF vertex;
    PointF second;
};

enum class AnnotationType
{
    Rectangle,
    Ellipse,
    Polygon,
    Polyline,
    Point,
    Arrow,
    Text,
    Ruler,
    Angle,
};

/// A sequence of vertices, closed into a polygon (FR-ANN-03) or left open as a
/// freehand line (FR-ANN-04).
///
/// One type for both, because they differ only in whether the path closes:
/// the vertices, the bounding box, the translation and the edge hit-test are
/// identical, and only the closing segment, the fill and the interior test
/// differ. Two variant alternatives would duplicate all of that to express
/// one boolean.
///
/// Self-intersecting outlines are valid. Pathologists produce them by accident
/// when tracing quickly, and silently rejecting or repairing someone's outline
/// is worse than rendering what they drew; the even-odd rule handles them.
struct PolygonGeometry
{
    std::vector<PointF> vertices;
    bool closed = true;
};

/// Fewer vertices than this cannot describe the shape: a closed polygon needs
/// three, an open path two. Enforced by the drawing tool at commit and by the
/// serializer on both read and write, so the application cannot produce a file
/// it would refuse to open.
constexpr std::size_t kMinPolygonVertices = 3;
constexpr std::size_t kMinPolylineVertices = 2;

/// Vertices in an ellipse's outline. Fixed, never adaptive and never derived
/// from the shape's size or the current zoom: an adaptive count would make a
/// stored annotation's measured area depend on the zoom it happened to be
/// measured at, and sub-project G exists to make measurements reproducible.
///
/// At 512 the area reads about 0.0025% under the true ellipse and the facet
/// bulge is about 0.19 px on a shape 20000 screen pixels wide. See the design
/// spec 3.2 before changing it -- both numbers move with it.
constexpr std::size_t kEllipseOutlineVertices = 512;

// Appended, never inserted: the variant default-constructs to its FIRST
// alternative, and AnnotationSerialization.cpp relies on that when it builds a
// placeholder Annotation to parse into.
using AnnotationGeometry = std::variant<RectangleGeometry, PolygonGeometry, EllipseGeometry,
                                        PointGeometry, ArrowGeometry, TextGeometry, RulerGeometry,
                                        AngleGeometry>;

/// Always positive-extent: a shape built from an inverted drag is normalised.
[[nodiscard]] RectF boundingBox(const AnnotationGeometry& geom);

/// True when `point` lies inside the shape or within `toleranceSlideUnits` of
/// its outline. Filled annotations (FR-ANN-11 gives them a fill opacity) must
/// be clickable in their interior.
///
/// The tolerance is in SLIDE units. Callers hold a screen-pixel tolerance and
/// convert with ui::screenToleranceToSlide, which keeps the only
/// zoom-dependent arithmetic at one call site and this function pure.
/// A negative tolerance is treated as zero.
[[nodiscard]] bool hitTest(const AnnotationGeometry& geom, PointF point,
                           double toleranceSlideUnits);

/// The shape as a sequence of points to stroke, in order, plus whether the
/// last point joins back to the first.
///
/// This is the whole of what the painter needs to know about a geometry, and
/// it is plain data: the Qt-side conversion to a QPainterPath is a loop with
/// no knowledge of shape types. Keeping the per-type decision here is also
/// what makes it testable -- no test binary in this repository creates a
/// QApplication, so nothing inside a widget can be covered.
struct Outline
{
    std::vector<PointF> points;
    bool closed = true;
};

/// The kind of shape `geom` holds. The single source of truth for an
/// annotation's type; Annotation::type() delegates here.
[[nodiscard]] AnnotationType typeOf(const AnnotationGeometry& geom);

/// `geom` as points to stroke. A rectangle yields its four corners from the
/// normalised box, so the painted outline matches the box hitTest uses.
[[nodiscard]] Outline outline(const AnnotationGeometry& geom);

/// `geom` moved by a slide-space delta, shape and vertex count preserved.
///
/// Returns a whole geometry rather than a rectangle: the move handler used to
/// rebuild every annotation from its bounding box, which would have turned a
/// polygon into a rectangle the first time a user dragged it.
[[nodiscard]] AnnotationGeometry translated(const AnnotationGeometry& geom, double dx, double dy);

/// The point on segment `ab` nearest `point`, clamped to the segment rather
/// than its infinite line. A zero-length segment yields its own endpoint.
///
/// Public because core::nearestEdge needs the projection itself, not just the
/// distance to it, and duplicating the maths in two translation units is how
/// the two come to disagree.
[[nodiscard]] PointF closestPointOnSegment(PointF a, PointF b, PointF point);

/// True when this shape TYPE always has a well-defined area, whatever its
/// values -- a property of the type, not of the instance.
///
/// core::measure() uses it to skip the O(n^2) self-intersection test, which at
/// kEllipseOutlineVertices costs about 131000 segment-pair comparisons. A
/// polygon's vertices are the user's, so nothing can promise anything about
/// them; a shape generated from a box encloses one region by construction.
///
/// **Not a claim of convexity, and not a claim that the outline cannot
/// self-intersect.** Two earlier names said each of those and both were false
/// for the same inputs: a degenerate ellipse or a zero-extent rectangle --
/// which the parser accepts from disk on purpose (design spec 2.2) -- is
/// collinear rather than convex, and its generated vertices DO overlap by
/// isSelfIntersecting's collinear rule. Its area is nonetheless perfectly well
/// defined: zero. That is the only property promised here, and the only one
/// the caller needs. A future use that needs real convexity must not reach for
/// this.
[[nodiscard]] bool hasWellDefinedArea(const AnnotationGeometry& geom);

} // namespace slideio::viewer::core
