#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <utility>

using namespace slideio::viewer::ui;
namespace core = slideio::viewer::core;

namespace
{

core::Annotation annotationWith(core::AnnotationGeometry geometry)
{
    return core::Annotation("id-1", std::move(geometry));
}

} // namespace

// shapeName dispatches over the variant and so cannot be forgotten silently.
// shapeTypeName is a switch with an empty-string fallback, which CAN be: a
// missed case shows the user a blank Type column rather than a compile error.
TEST_CASE("every geometry type has a user-visible name", "[ui][AnnotationShapeName]")
{
    REQUIRE(shapeName(annotationWith(
                core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}}))
            == QStringLiteral("Rectangle"));
    REQUIRE(shapeName(annotationWith(core::PointGeometry{core::PointF{1.0, 2.0}}))
            == QStringLiteral("Point"));
    REQUIRE(shapeName(annotationWith(
                core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}}))
            == QStringLiteral("Arrow"));
}

TEST_CASE("shapeTypeName answers for the point and arrow types",
          "[ui][AnnotationShapeName]")
{
    REQUIRE(shapeTypeName(core::AnnotationType::Point) == QStringLiteral("Point"));
    REQUIRE(shapeTypeName(core::AnnotationType::Arrow) == QStringLiteral("Arrow"));
    // Not the empty string the fallback would give, which is what a missed
    // case looks like.
    REQUIRE_FALSE(shapeTypeName(core::AnnotationType::Point).isEmpty());
    REQUIRE_FALSE(shapeTypeName(core::AnnotationType::Arrow).isEmpty());
}

// The two words must be the same whichever route a caller takes, or the list
// panel's Type column and the properties panel's Type row disagree about one
// annotation.
TEST_CASE("shapeName and shapeTypeName agree", "[ui][AnnotationShapeName]")
{
    const core::Annotation marker = annotationWith(core::PointGeometry{core::PointF{1.0, 2.0}});
    REQUIRE(shapeName(marker) == shapeTypeName(marker.type()));

    const core::Annotation arrow = annotationWith(
        core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(shapeName(arrow) == shapeTypeName(arrow.type()));
}

TEST_CASE("a text is called Text", "[ui][AnnotationShapeName]")
{
    const core::Annotation text = annotationWith(core::TextGeometry{core::PointF{1.0, 2.0}});
    REQUIRE(shapeName(text) == QStringLiteral("Text"));
    REQUIRE(shapeTypeName(core::AnnotationType::Text) == QStringLiteral("Text"));
}

TEST_CASE("a ruler and an angle are named", "[ui][AnnotationShapeName]")
{
    CHECK(shapeTypeName(core::AnnotationType::Ruler) == QStringLiteral("Ruler"));
    CHECK(shapeTypeName(core::AnnotationType::Angle) == QStringLiteral("Angle"));
    const core::Annotation ruler =
        annotationWith(core::RulerGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    CHECK(shapeName(ruler) == QStringLiteral("Ruler"));
}
