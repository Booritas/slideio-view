#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationMerge.h"

#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

std::chrono::system_clock::time_point at(int64_t secondsSinceEpoch, int64_t millis = 0)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(secondsSinceEpoch)
                                                 + std::chrono::milliseconds(millis));
}

core::Annotation annotation(const std::string& id, int64_t modifiedSeconds, int64_t modifiedMillis = 0,
                            double x = 0.0)
{
    core::AnnotationProperties properties;
    properties.label = id;
    core::AnnotationMetadata metadata;
    metadata.createdAt = at(modifiedSeconds);
    metadata.modifiedAt = at(modifiedSeconds, modifiedMillis);
    return core::Annotation(id,
                            core::RectangleGeometry{core::PointF{x, 0.0}, core::PointF{x + 1.0, 1.0}},
                            properties, metadata);
}

std::vector<std::string> ids(const std::vector<core::Annotation>& annotations)
{
    std::vector<std::string> result;
    result.reserve(annotations.size());
    for (const auto& one : annotations) {
        result.push_back(one.id());
    }
    return result;
}

} // namespace

TEST_CASE("disjoint sets are unioned, current first", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 100), annotation("b", 100)};
    const std::vector<core::Annotation> incoming{annotation("c", 100), annotation("d", 100)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(ids(outcome.annotations) == std::vector<std::string>{"a", "b", "c", "d"});
    REQUIRE(outcome.adopted == 2);
    REQUIRE(outcome.superseded == 0);
    REQUIRE(outcome.kept == 0);
}

TEST_CASE("a newer incoming annotation supersedes in place", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 100), annotation("b", 100),
                                                annotation("c", 100)};
    const std::vector<core::Annotation> incoming{annotation("b", 200, 0, 50.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    // The slot is what matters: index order is paint order and hit-test order.
    REQUIRE(ids(outcome.annotations) == std::vector<std::string>{"a", "b", "c"});
    REQUIRE(outcome.annotations[1].boundingBox().x == 50.0);
    REQUIRE(outcome.superseded == 1);
    REQUIRE(outcome.kept == 0);
}

TEST_CASE("an older incoming annotation loses", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 200, 0, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 0, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations.size() == 1);
    REQUIRE(outcome.annotations[0].boundingBox().x == 7.0);
    REQUIRE(outcome.kept == 1);
    REQUIRE(outcome.superseded == 0);
}

TEST_CASE("an exact tie keeps the on-screen annotation", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 100, 0, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 0, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations[0].boundingBox().x == 7.0);
    REQUIRE(outcome.kept == 1);
}

TEST_CASE("timestamps differing only below the second compare equal", "[core][AnnotationMerge]")
{
    // formatIso8601Utc truncates to whole seconds (src/core/src/Iso8601.cpp:107),
    // so the sub-second part cannot survive a round-trip. Honouring it here would
    // make the same merge give a different answer before and after a save.
    const std::vector<core::Annotation> current{annotation("a", 100, 100, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 900, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations[0].boundingBox().x == 7.0);
    REQUIRE(outcome.kept == 1);
    REQUIRE(outcome.superseded == 0);
}

TEST_CASE("an unstamped annotation loses to a real timestamp", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 0, 0, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 0, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations[0].boundingBox().x == 99.0);
    REQUIRE(outcome.superseded == 1);
}

TEST_CASE("a repeated id inside one set drops nothing", "[core][AnnotationMerge]")
{
    // A hand-edited file can hold the same id twice. Neither copy is discarded
    // and neither is matched twice: the first occurrence is the one compared.
    const std::vector<core::Annotation> current{annotation("a", 100, 0, 1.0),
                                                annotation("a", 100, 0, 2.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 300, 0, 3.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations.size() == 2);
    REQUIRE(outcome.annotations[0].boundingBox().x == 3.0);
    REQUIRE(outcome.annotations[1].boundingBox().x == 2.0);
    REQUIRE(outcome.superseded == 1);
}

TEST_CASE("a repeated id inside the incoming set collapses onto one slot",
          "[core][AnnotationMerge]")
{
    // The two sides are not symmetric, and the header must not claim they are.
    // A repeat inside `current` keeps both copies because only the first is
    // indexed; a repeat inside `incoming` meets the slot its own first copy
    // just created and is compared against it like any other collision.
    const std::vector<core::Annotation> incoming{annotation("x", 100, 0, 1.0),
                                                 annotation("x", 300, 0, 2.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations({}, incoming);

    REQUIRE(outcome.annotations.size() == 1);
    REQUIRE(outcome.annotations[0].boundingBox().x == 2.0);
    REQUIRE(outcome.adopted == 1);
    REQUIRE(outcome.superseded == 1);
}

TEST_CASE("empty inputs", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> one{annotation("a", 100)};

    REQUIRE(core::mergeAnnotations({}, {}).annotations.empty());
    REQUIRE(core::mergeAnnotations(one, {}).annotations.size() == 1);
    REQUIRE(core::mergeAnnotations({}, one).annotations.size() == 1);
    REQUIRE(core::mergeAnnotations({}, one).adopted == 1);
}
