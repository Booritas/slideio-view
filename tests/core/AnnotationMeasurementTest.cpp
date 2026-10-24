#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <cmath>
#include <string>
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
    // Unconditional: the zero-extent override is the only thing stopping a
    // coincident-vertex polygon from reading "self-intersecting", and a
    // permissive check would pass either way.
    REQUIRE(coincident.area.has_value());
    REQUIRE(*coincident.area == 0.0);
    REQUIRE_FALSE(coincident.selfIntersecting);
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

TEST_CASE("lengths below a millimetre read in microns", "[core][AnnotationMeasurement]")
{
    // 245 um = 245e-6 m.
    REQUIRE(core::formatLength(245e-6, core::MeasurementUnits::Physical) == "245 \xC2\xB5m");
    REQUIRE(core::formatLength(1e-6, core::MeasurementUnits::Physical) == "1.00 \xC2\xB5m");
    REQUIRE(core::formatLength(0.5e-6, core::MeasurementUnits::Physical) == "0.500 \xC2\xB5m");
}

TEST_CASE("lengths at or above a millimetre read in millimetres",
          "[core][AnnotationMeasurement]")
{
    REQUIRE(core::formatLength(1e-3, core::MeasurementUnits::Physical) == "1.00 mm");
    REQUIRE(core::formatLength(1.234e-3, core::MeasurementUnits::Physical) == "1.23 mm");
    REQUIRE(core::formatLength(15e-3, core::MeasurementUnits::Physical) == "15.0 mm");
}

TEST_CASE("a length rounding up to the threshold switches unit",
          "[core][AnnotationMeasurement]")
{
    // Review Focus 4. 999.6 um is 1000 um at three significant figures.
    // Printing "1000 um" would be four significant figures AND the wrong unit.
    REQUIRE(core::formatLength(999.6e-6, core::MeasurementUnits::Physical) == "1.00 mm");
    REQUIRE(core::formatLength(999.4e-6, core::MeasurementUnits::Physical) == "999 \xC2\xB5m");
}

TEST_CASE("areas pick their unit at one square millimetre",
          "[core][AnnotationMeasurement]")
{
    // 1 mm^2 = 1e-6 m^2. 872 um^2 = 872e-12 m^2.
    REQUIRE(core::formatArea(872e-12, core::MeasurementUnits::Physical) == "872 \xC2\xB5m\xC2\xB2");
    REQUIRE(core::formatArea(1e-6, core::MeasurementUnits::Physical) == "1.00 mm\xC2\xB2");
    REQUIRE(core::formatArea(4.56e-6, core::MeasurementUnits::Physical) == "4.56 mm\xC2\xB2");
}

TEST_CASE("a whole-slide area stays in millimetres, never scientific",
          "[core][AnnotationMeasurement]")
{
    // Review Focus 5. 15 mm x 15 mm = 225 mm^2 = 2.25e-4 m^2.
    const std::string shown = core::formatArea(2.25e-4, core::MeasurementUnits::Physical);
    REQUIRE(shown == "225 mm\xC2\xB2");
    REQUIRE(shown.find('e') == std::string::npos);
    REQUIRE(shown.find('E') == std::string::npos);
}

TEST_CASE("pixel measurements are labelled and carry no separators",
          "[core][AnnotationMeasurement]")
{
    REQUIRE(core::formatLength(1480.0, core::MeasurementUnits::Pixels) == "1480 px");
    REQUIRE(core::formatArea(12480.0, core::MeasurementUnits::Pixels) == "12480 px\xC2\xB2");
    // Locale-free: grouping is a presentation decision and core does not make it.
    REQUIRE(core::formatArea(12480.0, core::MeasurementUnits::Pixels).find(',') == std::string::npos);
}

TEST_CASE("zero formats cleanly in both unit systems", "[core][AnnotationMeasurement]")
{
    REQUIRE(core::formatLength(0.0, core::MeasurementUnits::Physical) == "0.00 \xC2\xB5m");
    REQUIRE(core::formatArea(0.0, core::MeasurementUnits::Pixels) == "0 px\xC2\xB2");
}

TEST_CASE("three significant figures means three, not however many fit",
          "[core][AnnotationMeasurement]")
{
    // With zero decimals, printing the RAW value keeps every integer digit.
    // 234567 um^2 is 235000 at three figures; showing it in full asserts a
    // precision the pixel grid cannot support, in a number meant for a report.
    REQUIRE(core::formatArea(234567e-12, core::MeasurementUnits::Physical)
            == "235000 \xC2\xB5m\xC2\xB2");

    // And rounding across the threshold promotes, exactly as 999.6 um does.
    REQUIRE(core::formatArea(999999e-12, core::MeasurementUnits::Physical) == "1.00 mm\xC2\xB2");
}

TEST_CASE("a half-calibrated slide does not invent a self-intersection",
          "[core][AnnotationMeasurement]")
{
    // resolutionX malformed as 0 while resolutionY is valid: every vertex
    // collapses onto a vertical line, where a collinear-overlap test would fire.
    // The shape is simple, and "self-intersecting" is a stated REASON -- giving
    // a false one is worse than giving none. Self-intersection is a property of
    // the shape, and positive scaling cannot create or remove it.
    // A square, not a triangle: a triangle's three edges are all mutually
    // adjacent, so it can never report a crossing and would pass either way.
    // Collapsed, the square's second and fourth edges lie exactly on top of
    // each other, which is precisely what the collinear-overlap rule catches.
    const core::ShapeMetrics m =
        core::measure(polygon({{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, true),
                      core::PixelSize{0.0, 5e-7});
    REQUIRE_FALSE(m.selfIntersecting);
    REQUIRE(m.area.has_value());
    REQUIRE(*m.area == 0.0);
}

// A degenerate ellipse's 512 vertices are collinear, which trips
// isSelfIntersecting's collinear-overlap rule. Before the convexity
// short-circuit that made measure() report "self-intersecting, area unknown"
// for a shape whose area is plainly zero -- a stated reason that is false,
// which the header on isSelfIntersecting calls worse than stating none.
TEST_CASE("a degenerate ellipse measures zero rather than self-intersecting",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry flat =
        core::EllipseGeometry{core::PointF{0.0, 25.0}, core::PointF{100.0, 25.0}};
    const core::ShapeMetrics metrics = core::measure(flat, core::PixelSize{});

    REQUIRE_FALSE(metrics.selfIntersecting);
    REQUIRE(metrics.closed);
    REQUIRE(metrics.area.has_value());
    REQUIRE(std::abs(*metrics.area) < 1e-9);
}

TEST_CASE("an ellipse is never reported as self-intersecting",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry shape =
        core::EllipseGeometry{core::PointF{0.0, 0.0}, core::PointF{100.0, 50.0}};
    const core::ShapeMetrics metrics = core::measure(shape, core::PixelSize{});

    REQUIRE_FALSE(metrics.selfIntersecting);
    REQUIRE(metrics.area.has_value());
}

// The inscribed polygon is the definition of the shape, so this is the exact
// expected value, not an approximation of one. The second assertion documents
// the relationship design spec 3.2 claims and fails if kEllipseOutlineVertices
// changes without that spec changing.
TEST_CASE("an ellipse measures its inscribed polygon's area",
          "[core][AnnotationMeasurement]")
{
    const double a = 50.0;
    const double b = 25.0;
    const core::AnnotationGeometry shape =
        core::EllipseGeometry{core::PointF{0.0, 0.0}, core::PointF{2.0 * a, 2.0 * b}};
    const core::ShapeMetrics metrics = core::measure(shape, core::PixelSize{});

    REQUIRE(metrics.area.has_value());

    const double n = static_cast<double>(core::kEllipseOutlineVertices);
    const double exactPolygonArea = 0.5 * n * a * b * std::sin(2.0 * 3.14159265358979323846 / n);
    REQUIRE(std::abs(*metrics.area - exactPolygonArea) < 1e-6);

    const double trueArea = 3.14159265358979323846 * a * b;
    REQUIRE(std::abs(*metrics.area - trueArea) / trueArea < 0.0001);
}

TEST_CASE("a circle measures pi r squared within the documented tolerance",
          "[core][AnnotationMeasurement]")
{
    const double r = 40.0;
    const core::AnnotationGeometry circle =
        core::EllipseGeometry{core::PointF{0.0, 0.0}, core::PointF{2.0 * r, 2.0 * r}};
    const core::ShapeMetrics metrics = core::measure(circle, core::PixelSize{});

    REQUIRE(metrics.area.has_value());
    const double trueArea = 3.14159265358979323846 * r * r;
    REQUIRE(std::abs(*metrics.area - trueArea) / trueArea < 0.0001);
}

// Observed 2026-10-17 on a real 160-vertex trace of Philips-1.tiff: closing a
// polygon by double-click appends a vertex identical to the last trailed one,
// which makes segment n-3 END where the closing segment BEGINS. They share a
// point, the adjacency rule only skips segments adjacent by INDEX, and the
// shape was reported self-intersecting -- withholding an area that is plainly
// 100. The shape is fine; the duplicate is invisible and means nothing.
TEST_CASE("a polygon whose close duplicated its last vertex is not self-intersecting",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry square = core::PolygonGeometry{
        {core::PointF{0.0, 0.0}, core::PointF{10.0, 0.0}, core::PointF{10.0, 10.0},
         core::PointF{0.0, 10.0}, core::PointF{0.0, 10.0}},
        true};

    const core::ShapeMetrics metrics = core::measure(square, core::PixelSize{});

    REQUIRE_FALSE(metrics.selfIntersecting);
    REQUIRE(metrics.area.has_value());
    REQUIRE(*metrics.area == 100.0);
}

// The same degeneracy by the other route: a trace that ends exactly where it
// started makes the CLOSING segment zero-length.
TEST_CASE("a closed polygon whose last vertex repeats its first is not self-intersecting",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry square = core::PolygonGeometry{
        {core::PointF{0.0, 0.0}, core::PointF{10.0, 0.0}, core::PointF{10.0, 10.0},
         core::PointF{0.0, 10.0}, core::PointF{0.0, 0.0}},
        true};

    const core::ShapeMetrics metrics = core::measure(square, core::PixelSize{});

    REQUIRE_FALSE(metrics.selfIntersecting);
    REQUIRE(metrics.area.has_value());
    REQUIRE(*metrics.area == 100.0);
}

// A real crossing must still be caught -- the fix removes degenerate segments,
// not the detector.
TEST_CASE("a genuinely crossed polygon is still self-intersecting",
          "[core][AnnotationMeasurement]")
{
    const core::AnnotationGeometry bowtie = core::PolygonGeometry{
        {core::PointF{0.0, 0.0}, core::PointF{10.0, 10.0}, core::PointF{10.0, 0.0},
         core::PointF{0.0, 10.0}},
        true};

    const core::ShapeMetrics metrics = core::measure(bowtie, core::PixelSize{});

    REQUIRE(metrics.selfIntersecting);
    REQUIRE_FALSE(metrics.area.has_value());
}

// Spec 2.3: measure() is driven by outline(), so a new geometry type needs no
// overload here. These tests are what make that a checked claim rather than an
// assumption.
TEST_CASE("a point measures length zero with no area",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics metrics =
        core::measure(core::PointGeometry{core::PointF{50.0, 50.0}}, core::PixelSize{});
    REQUIRE(metrics.outlineLength == 0.0);
    REQUIRE_FALSE(metrics.area.has_value());
    REQUIRE_FALSE(metrics.closed);
    REQUIRE_FALSE(metrics.selfIntersecting);
}

// closed == false is what makes the properties panel caption this "Length"
// rather than "Perimeter".
TEST_CASE("an arrow measures its tail-to-head distance with no area",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics metrics = core::measure(
        core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{30.0, 40.0}},
        core::PixelSize{});
    REQUIRE(metrics.outlineLength == 50.0);
    REQUIRE_FALSE(metrics.area.has_value());
    REQUIRE_FALSE(metrics.closed);
}

// Review Focus 1: accepted from disk, and it must measure rather than produce
// a NaN.
TEST_CASE("a zero-length arrow measures length zero",
          "[core][AnnotationMeasurement]")
{
    const core::ShapeMetrics metrics = core::measure(
        core::ArrowGeometry{core::PointF{12.0, 34.0}, core::PointF{12.0, 34.0}},
        core::PixelSize{});
    REQUIRE(metrics.outlineLength == 0.0);
    REQUIRE_FALSE(metrics.area.has_value());
}

// --- extreme values --------------------------------------------------------
//
// None of these come from a drawing gesture. They come from a hand-edited
// annotation file, which this format is explicitly meant to be: coordinates
// large or small enough that the unit conversion in formatLength and
// formatArea overflows or underflows the double it is applied to.

TEST_CASE("formatLength renders a huge value in full", "[core][Measurement]")
{
    // 1e300 m is 1e303 mm: 304 integer digits at no decimals. The buffer used
    // to be 64 bytes, and snprintf truncated the number in the middle rather
    // than overrunning -- safe, and unreadable, and still shaped like a
    // measurement.
    const std::string text = core::formatLength(1e300, core::MeasurementUnits::Physical);

    REQUIRE(text.size() > 300);
    REQUIRE(text.substr(text.size() - 3) == " mm");
    REQUIRE(text.find('.') == std::string::npos);
}

TEST_CASE("formatLength renders a tiny value as zero to its last place",
          "[core][Measurement]")
{
    // Three significant figures of 1e-300 would ask for 302 decimals. The
    // decimal count is clamped, so what comes out is a run of zeroes --
    // honest about the magnitude, and terminated.
    const std::string text = core::formatLength(1e-300, core::MeasurementUnits::Physical);

    REQUIRE(text.substr(text.size() - 3) == "\xC2\xB5m");
    REQUIRE(text.find("0.0") == 0);
    REQUIRE(text.size() < 80);
}

TEST_CASE("formatArea survives an overflowing unit conversion", "[core][Measurement]")
{
    // value * 1e12 is infinity for anything above about 1.8e296, and the
    // square-micron attempt is made first. log10 of infinity is infinity and
    // the cast to int was undefined behaviour; the non-finite guard takes it
    // first and renders "inf", which is then below no threshold, so the
    // function falls through to square millimetres and reports a finite
    // number there. The unit promotion is doing the recovering -- the guard
    // only has to stop the arithmetic from being undefined.
    const std::string text = core::formatArea(1e300, core::MeasurementUnits::Physical);

    REQUIRE(text.substr(text.size() - 5) == " mm\xC2\xB2");
    REQUIRE(text.find("inf") == std::string::npos);
    REQUIRE(text.size() > 300);
}
TEST_CASE("formatLength names a NaN rather than printing a number",
          "[core][Measurement]")
{
    const std::string text =
        core::formatLength(std::nan(""), core::MeasurementUnits::Physical);

    REQUIRE(text.find("nan") != std::string::npos);
}

TEST_CASE("measure and measureKnown agree for a hand-traced polygon",
          "[core][Measurement]")
{
    // measure() now hands measureOutline the outline it already built for the
    // self-intersection test instead of letting it build a second one. The two
    // entry points must still answer identically.
    const core::AnnotationGeometry geometry = core::PolygonGeometry{
        {core::PointF{0.0, 0.0}, core::PointF{10.0, 0.0}, core::PointF{10.0, 8.0},
         core::PointF{0.0, 8.0}},
        true};
    const core::PixelSize pixelSize{1e-6, 1e-6};

    const core::ShapeMetrics viaMeasure = core::measure(geometry, pixelSize);
    const core::ShapeMetrics viaKnown = core::measureKnown(geometry, pixelSize, false);

    REQUIRE(viaMeasure.area == viaKnown.area);
    REQUIRE(viaMeasure.outlineLength == viaKnown.outlineLength);
    REQUIRE(viaMeasure.closed == viaKnown.closed);
    REQUIRE(viaMeasure.selfIntersecting == viaKnown.selfIntersecting);
}

TEST_CASE("a ruler measures its length", "[core][AnnotationMeasurement][Ruler]")
{
    const core::ShapeMetrics metrics =
        core::measure(core::RulerGeometry{core::PointF{0.0, 0.0}, core::PointF{3.0, 4.0}}, core::PixelSize{});
    CHECK(metrics.outlineLength == 5.0);
    CHECK_FALSE(metrics.area.has_value());
    CHECK_FALSE(metrics.angleDegrees.has_value());
}

TEST_CASE("an angle measures between its rays, 0 to 180", "[core][AnnotationMeasurement][Angle]")
{
    const core::PointF o{0.0, 0.0};
    const auto angleOf = [&](core::PointF a, core::PointF b) {
        return core::angleDegrees(core::AngleGeometry{a, o, b}, core::PixelSize{});
    };
    REQUIRE(angleOf({1, 0}, {0, 1}).has_value());
    CHECK(*angleOf({1, 0}, {0, 1}) == Catch::Approx(90.0));
    CHECK(*angleOf({1, 0}, {-1, 0}) == Catch::Approx(180.0));
    CHECK(*angleOf({1, 0}, {2, 0}) == Catch::Approx(0.0).margin(1e-9));
    CHECK_FALSE(angleOf({0, 0}, {0, 1}).has_value());
    CHECK_FALSE(angleOf({1, 0}, {0, 0}).has_value());
}

TEST_CASE("non-square pixels change an angle", "[core][AnnotationMeasurement][Angle]")
{
    // 45 degrees in pixel space; at 1 x 2 um/px the second ray is (1, 2).
    const auto degrees = core::angleDegrees(
        core::AngleGeometry{core::PointF{1, 0}, core::PointF{0, 0}, core::PointF{1, 1}}, core::PixelSize{1.0, 2.0});
    REQUIRE(degrees.has_value());
    CHECK(*degrees == Catch::Approx(63.4349488).epsilon(1e-6));
}

TEST_CASE("measure() carries an angle's degrees and nothing else's", "[core][AnnotationMeasurement][Angle]")
{
    const core::AngleGeometry right{core::PointF{1, 0}, core::PointF{0, 0}, core::PointF{0, 1}};
    const core::ShapeMetrics metrics = core::measure(right, core::PixelSize{});
    REQUIRE(metrics.angleDegrees.has_value());
    CHECK(*metrics.angleDegrees == Catch::Approx(90.0));
    CHECK_FALSE(core::measure(core::ArrowGeometry{core::PointF{0, 0}, core::PointF{1, 1}}, core::PixelSize{})
                    .angleDegrees.has_value());
}
