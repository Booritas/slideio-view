#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationImport.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/core/AnnotationGeoJson.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer::app;
namespace core = slideio::viewer::core;

namespace
{

core::Annotation marker(std::string id)
{
    return core::Annotation{std::move(id), core::PointGeometry{core::PointF{1.0, 2.0}}};
}

} // namespace

TEST_CASE("planImport mints ids for empty ones", "[app][AnnotationImport]")
{
    AnnotationModel model;
    const ImportPlan plan = planImport({marker(""), marker("")}, model);

    REQUIRE(plan.toAdd.size() == 2);
    REQUIRE_FALSE(plan.toAdd[0].id().empty());
    REQUIRE_FALSE(plan.toAdd[1].id().empty());
    REQUIRE(plan.toAdd[0].id() != plan.toAdd[1].id());
    REQUIRE(plan.alreadyPresent == 0);
}

TEST_CASE("planImport keeps everything but the id of a minted annotation", "[app][AnnotationImport]")
{
    core::AnnotationProperties properties;
    properties.label = "kept";
    core::AnnotationMetadata metadata;
    metadata.author = "someone";
    AnnotationModel model;
    const ImportPlan plan =
        planImport({core::Annotation{"", core::PointGeometry{core::PointF{3.0, 4.0}}, properties, metadata}}, model);

    REQUIRE(plan.toAdd.size() == 1);
    REQUIRE(plan.toAdd[0].properties().label == "kept");
    REQUIRE(plan.toAdd[0].metadata().author == "someone");
    REQUIRE(plan.toAdd[0].boundingBox().x == 3.0);
}

TEST_CASE("planImport skips ids already in the scene", "[app][AnnotationImport]")
{
    AnnotationModel model;
    model.insert(marker("a"));
    const ImportPlan plan = planImport({marker("a"), marker("b")}, model);

    REQUIRE(plan.toAdd.size() == 1);
    REQUIRE(plan.toAdd[0].id() == "b");
    REQUIRE(plan.alreadyPresent == 1);
}

TEST_CASE("planImport skips an id repeated in the file", "[app][AnnotationImport]")
{
    AnnotationModel model;
    const ImportPlan plan = planImport({marker("b"), marker("b")}, model);

    REQUIRE(plan.toAdd.size() == 1);
    REQUIRE(plan.alreadyPresent == 1);
}

TEST_CASE("re-importing an export adds nothing", "[app][AnnotationImport]")
{
    AnnotationModel model;
    model.add(core::PointGeometry{core::PointF{1.0, 1.0}});
    model.add(core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{5.0, 5.0}});
    model.add(core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{9.0, 9.0}});

    const auto now = std::chrono::system_clock::now();
    const core::GeoJsonExportResult exported =
        core::annotationsToGeoJson(model.annotations(), core::GeoJsonExportContext{}, now);
    REQUIRE(exported.error.empty());
    core::GeoJsonImportResult read = core::geoJsonToAnnotations(exported.text, now);
    REQUIRE(read.error.empty());

    const ImportPlan plan = planImport(std::move(read.annotations), model);

    REQUIRE(plan.toAdd.empty());
    REQUIRE(plan.alreadyPresent == 3);
}
