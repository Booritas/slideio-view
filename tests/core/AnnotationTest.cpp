#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/Annotation.h"

#include <chrono>
#include <string>
#include <utility>

using namespace slideio::viewer::core;

namespace
{

Annotation makeRect(std::string id = "a1")
{
    return Annotation(std::move(id), AnnotationType::Rectangle,
                      RectangleGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});
}

/// Blocks until system_clock has advanced past `t`, so a strict timestamp
/// comparison is meaningful rather than flaky. Bounded, so a clock that never
/// advances fails the test instead of hanging the suite.
void waitForClockToPass(std::chrono::system_clock::time_point t)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::system_clock::now() <= t) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL("system_clock did not advance within 2 seconds");
        }
    }
}

} // namespace

TEST_CASE("an annotation keeps the id and type it was built with", "[core][Annotation]")
{
    const Annotation annotation = makeRect("abc123");
    REQUIRE(annotation.id() == "abc123");
    REQUIRE(annotation.type() == AnnotationType::Rectangle);
}

TEST_CASE("an annotation delegates its bounding box to the geometry", "[core][Annotation]")
{
    const RectF box = makeRect().boundingBox();
    REQUIRE(box.x == 10.0);
    REQUIRE(box.y == 20.0);
    REQUIRE(box.width == 100.0);
    REQUIRE(box.height == 50.0);
}

TEST_CASE("containsPoint follows the geometry's hit test", "[core][Annotation]")
{
    const Annotation annotation = makeRect();
    REQUIRE(annotation.containsPoint(PointF{50.0, 40.0}, 0.0));
    REQUIRE_FALSE(annotation.containsPoint(PointF{5.0, 45.0}, 0.0));
    REQUIRE(annotation.containsPoint(PointF{5.0, 45.0}, 5.0));
}

TEST_CASE("a new annotation carries the default palette colour", "[core][Annotation]")
{
    const AnnotationProperties props = makeRect().properties();
    REQUIRE(props.color.r == 0xE6);
    REQUIRE(props.color.g == 0x7E);
    REQUIRE(props.color.b == 0x22);
    REQUIRE(props.color.a == 0xFF);
}

TEST_CASE("the author field starts empty pending user identity", "[core][Annotation]")
{
    // FR-USER-01 and FR-ANN-12 require an author, but user identity is Stage 4.
    // The field exists now so the struct does not change when it lands.
    REQUIRE(makeRect().metadata().author.empty());
}

TEST_CASE("setGeometry replaces the shape and refreshes the bounding box",
          "[core][Annotation]")
{
    Annotation annotation = makeRect();
    annotation.setGeometry(RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});

    const RectF box = annotation.boundingBox();
    REQUIRE(box.x == 0.0);
    REQUIRE(box.width == 10.0);
}

TEST_CASE("setGeometry refreshes modifiedAt", "[core][Annotation]")
{
    Annotation annotation = makeRect();
    const auto createdAt = annotation.metadata().createdAt;
    waitForClockToPass(createdAt);

    annotation.setGeometry(RectangleGeometry{PointF{0.0, 0.0}, PointF{10.0, 10.0}});
    REQUIRE(annotation.metadata().modifiedAt > createdAt);
}

TEST_CASE("setProperties refreshes modifiedAt", "[core][Annotation]")
{
    Annotation annotation = makeRect();
    const auto createdAt = annotation.metadata().createdAt;
    waitForClockToPass(createdAt);

    AnnotationProperties props;
    props.label = "updated";
    annotation.setProperties(props);
    REQUIRE(annotation.metadata().modifiedAt > createdAt);
}

TEST_CASE("setProperties replaces the properties", "[core][Annotation]")
{
    Annotation annotation = makeRect();
    AnnotationProperties props;
    props.label = "tumour margin";
    props.lineWidth = 4.0f;
    annotation.setProperties(props);

    REQUIRE(annotation.properties().label == "tumour margin");
    REQUIRE(annotation.properties().lineWidth == 4.0f);
}
