#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationImport.h"
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/core/AnnotationGeoJson.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <QString>
#include <QUuid>

#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer::app;
namespace core = slideio::viewer::core;

namespace
{

const std::string kUuidA = "0b1f6c1e-1111-4a2b-8c3d-000000000001";
const std::string kUuidB = "0b1f6c1e-1111-4a2b-8c3d-000000000002";
const std::string kUpperCaseUuid = "0B1F6C1E-1111-4A2B-8C3D-000000000003";

core::Annotation marker(std::string id)
{
    return core::Annotation{std::move(id), core::PointGeometry{core::PointF{1.0, 2.0}}};
}

/// A QuPath-style Points class: one MultiPoint feature with id `id`.
std::string pointsClass(const std::string& id, const std::string& coordinates)
{
    return R"({"type":"FeatureCollection","features":[{"type":"Feature","id":")" + id + R"(",)"
           R"("geometry":{"type":"MultiPoint","coordinates":)" + coordinates + R"(},)"
           R"("properties":{"objectType":"annotation","classification":{"name":"Ki67"}}}]})";
}

/// Reads `file` and plans its import into `model`, as MainWindow does.
ImportPlan plan(const std::string& file, const AnnotationModel& model)
{
    core::GeoJsonImportResult read = core::geoJsonToAnnotations(file, std::chrono::system_clock::now());
    REQUIRE(read.error.empty());
    return planImport(std::move(read.annotations), std::move(read.idSeeds), model);
}

bool isUuid(const std::string& id)
{
    return !QUuid::fromString(QString::fromStdString(id)).isNull();
}

} // namespace

TEST_CASE("planImport mints ids for empty ones", "[app][AnnotationImport]")
{
    AnnotationModel model;
    const ImportPlan plan = planImport({marker(""), marker("")}, {}, model);

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
    const ImportPlan plan = planImport(
        {core::Annotation{"", core::PointGeometry{core::PointF{3.0, 4.0}}, properties, metadata}}, {}, model);

    REQUIRE(plan.toAdd.size() == 1);
    REQUIRE(plan.toAdd[0].properties().label == "kept");
    REQUIRE(plan.toAdd[0].metadata().author == "someone");
    REQUIRE(plan.toAdd[0].boundingBox().x == 3.0);
}

TEST_CASE("a seeded part gets the same UUID every time", "[app][AnnotationImport]")
{
    AnnotationModel model;
    const ImportPlan first = planImport({marker(""), marker("")}, {"seed-a", "seed-b"}, model);
    const ImportPlan second = planImport({marker(""), marker("")}, {"seed-a", "seed-b"}, model);

    REQUIRE(first.toAdd.size() == 2);
    REQUIRE(first.toAdd[0].id() == second.toAdd[0].id());
    REQUIRE(first.toAdd[1].id() == second.toAdd[1].id());
    REQUIRE(first.toAdd[0].id() != first.toAdd[1].id());
    // A UUID, so QuPath can read it back: it parses an object id as java.util.UUID.
    REQUIRE(isUuid(first.toAdd[0].id()));
    REQUIRE(isUuid(first.toAdd[1].id()));
}

TEST_CASE("planImport skips ids already in the scene", "[app][AnnotationImport]")
{
    AnnotationModel model;
    model.insert(marker(kUuidA));
    const ImportPlan plan = planImport({marker(kUuidA), marker(kUuidB)}, {}, model);

    REQUIRE(plan.toAdd.size() == 1);
    REQUIRE(plan.toAdd[0].id() == kUuidB);
    REQUIRE(plan.alreadyPresent == 1);
}

TEST_CASE("planImport skips an id repeated in the file", "[app][AnnotationImport]")
{
    AnnotationModel model;
    const ImportPlan plan = planImport({marker(kUuidB), marker(kUuidB)}, {}, model);

    REQUIRE(plan.toAdd.size() == 1);
    REQUIRE(plan.alreadyPresent == 1);
}

TEST_CASE("an id that is not a UUID becomes a UUID derived from it", "[app][AnnotationImport]")
{
    // QuPath reads an object id as a UUID and replaces anything else, so an id
    // like "roi-7" kept as written would not survive viewer -> QuPath -> viewer,
    // and the third import would add everything again.
    AnnotationModel model;
    const ImportPlan first = planImport({marker("roi-7"), marker("17"), marker(kUpperCaseUuid)}, {}, model);
    const ImportPlan second = planImport({marker("roi-7"), marker("17"), marker(kUpperCaseUuid)}, {}, model);

    REQUIRE(first.toAdd.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(isUuid(first.toAdd[i].id()));
        REQUIRE(first.toAdd[i].id() == second.toAdd[i].id());
    }
    REQUIRE(first.toAdd[0].id() != "roi-7");
    REQUIRE(first.toAdd[0].id() != first.toAdd[1].id());
    // Only the canonical spelling -- lower case, no braces, as QuPath and this
    // application write it -- is kept as written.
    REQUIRE(first.toAdd[2].id() != kUpperCaseUuid);
}

TEST_CASE("a UUID id is kept as written", "[app][AnnotationImport]")
{
    AnnotationModel model;
    const ImportPlan plan = planImport({marker(kUuidA)}, {}, model);
    REQUIRE(plan.toAdd.size() == 1);
    REQUIRE(plan.toAdd[0].id() == kUuidA);
}

TEST_CASE("re-importing a file whose ids are not UUIDs adds nothing", "[app][AnnotationImport]")
{
    const std::string file =
        R"({"type":"FeatureCollection","features":[)"
        R"({"type":"Feature","id":"roi-7","geometry":{"type":"Point","coordinates":[1,2]},"properties":{}},)"
        R"({"type":"Feature","id":17,"geometry":{"type":"Point","coordinates":[3,4]},"properties":{}}]})";
    AnnotationModel model;
    model.insertMany(plan(file, model).toAdd);
    REQUIRE(model.annotations().size() == 2);

    const ImportPlan again = plan(file, model);

    REQUIRE(again.toAdd.empty());
    REQUIRE(again.alreadyPresent == 2);
}

TEST_CASE("re-importing a QuPath Points class adds nothing", "[app][AnnotationImport]")
{
    const std::string file = pointsClass("84a08285", "[[1,1],[2,2],[3,3],[4,4],[5,5]]");
    AnnotationModel model;
    model.insertMany(plan(file, model).toAdd);
    REQUIRE(model.annotations().size() == 5);

    const ImportPlan again = plan(file, model);

    REQUIRE(again.toAdd.empty());
    REQUIRE(again.alreadyPresent == 5);
}

TEST_CASE("re-importing an edited Points class adds only the new points", "[app][AnnotationImport]")
{
    // In QuPath: delete the FIRST point and add a new one. The class keeps its id.
    AnnotationModel model;
    model.insertMany(plan(pointsClass("84a08285", "[[1,1],[2,2],[3,3]]"), model).toAdd);
    REQUIRE(model.annotations().size() == 3);

    const ImportPlan edited = plan(pointsClass("84a08285", "[[2,2],[3,3],[4,4]]"), model);

    REQUIRE(edited.toAdd.size() == 1);
    REQUIRE(edited.alreadyPresent == 2);
    REQUIRE(edited.toAdd[0].boundingBox().x == 4.0);
}

TEST_CASE("re-importing an export adds nothing", "[app][AnnotationImport]")
{
    AnnotationModel model;
    model.add(core::PointGeometry{core::PointF{1.0, 1.0}});
    model.add(core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{5.0, 5.0}});
    model.add(core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{9.0, 9.0}});

    const core::GeoJsonExportResult exported = core::annotationsToGeoJson(
        model.annotations(), core::GeoJsonExportContext{}, std::chrono::system_clock::now());
    REQUIRE(exported.error.empty());

    const ImportPlan again = plan(exported.text, model);

    REQUIRE(again.toAdd.empty());
    REQUIRE(again.alreadyPresent == 3);
}
