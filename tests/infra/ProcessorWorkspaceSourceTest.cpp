#include "slideio/viewer/infra/ProcessorWorkspaceSource.h"

#include "support/ProcessorFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <algorithm>
#include <string>

using slideio::viewer::core::Rect;
using slideio::viewer::core::ResultsError;
using slideio::viewer::infra::ProcessorWorkspaceSource;
using slideio::viewer::test::ProcessorFixture;

namespace
{

constexpr int kTissue = 0;
constexpr int kNuclei = 1;
const Rect<double> kWholeRect{0.0, 0.0, 1000.0, 1000.0};

std::string tempPath(const QTemporaryDir& dir)
{
    return dir.path().toUtf8().toStdString();
}

int classIndex(const ProcessorWorkspaceSource& source, int layer, const std::string& name)
{
    const auto layers = source.layers();
    const auto& classes = layers.at(static_cast<size_t>(layer)).classes;
    for (size_t i = 0; i < classes.size(); ++i) {
        if (classes[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST_CASE("ProcessorWorkspaceSourceTest: OpenRefusesFolderWithoutStore", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const std::string dir = tempPath(temporary);
    try {
        ProcessorWorkspaceSource::open(dir);
        FAIL("open succeeded on a folder without workspace.store");
    } catch (const ResultsError& error) {
        const std::string message = error.what();
        CHECK(message.find("no workspace.store in") != std::string::npos);
        CHECK(message.find(dir) != std::string::npos);
    }
}

TEST_CASE("ProcessorWorkspaceSourceTest: ProvenanceIsRead", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    const auto provenance = source->provenance();
    CHECK(provenance.slidePath == "/slides/fixture.svs");
    CHECK(provenance.scene == 1);
}

TEST_CASE("ProcessorWorkspaceSourceTest: LayersAndClasses", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    const auto layers = source->layers();
    REQUIRE(layers.size() == 2);
    CHECK(layers[0].name == "tissue");
    CHECK(layers[0].objectCount == 1);
    CHECK(layers[0].classes.empty());
    CHECK(layers[0].stageState.empty());
    CHECK(layers[1].name == "nuclei");
    CHECK(layers[1].objectCount == 30);
    CHECK(layers[1].stageState.empty());
    // Equal counts sort by name: stroma before tumour. The asserted stroma label counts.
    REQUIRE(layers[1].classes.size() == 2);
    CHECK(layers[1].classes[0].name == "stroma");
    CHECK(layers[1].classes[0].objectCount == 10);
    CHECK(layers[1].classes[1].name == "tumour");
    CHECK(layers[1].classes[1].objectCount == 10);
}

TEST_CASE("ProcessorWorkspaceSourceTest: QueryReturnsIntersectingWithHoles", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    const auto answer = source->query(kTissue, Rect<double>{0.0, 0.0, 50.0, 50.0}, 100);
    CHECK_FALSE(answer.overBudget);
    CHECK(answer.inView == 1);
    REQUIRE(answer.objects.size() == 1);
    const auto& object = answer.objects[0];
    CHECK(object.id == fixture->tissueId());
    CHECK(object.layer == kTissue);
    CHECK(object.classes.empty());
    REQUIRE(object.rings.size() == 2);
    CHECK_FALSE(object.rings[0].hole);
    CHECK(object.rings[0].points.size() == 4);
    CHECK(object.rings[1].hole);
    CHECK(object.rings[1].points.size() == 4);
    CHECK(object.bounds.x == 0.0);
    CHECK(object.bounds.y == 0.0);
    CHECK(object.bounds.width == 1000.0);
    CHECK(object.bounds.height == 1000.0);
}

TEST_CASE("ProcessorWorkspaceSourceTest: QueryOverBudget", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    const auto answer = source->query(kNuclei, kWholeRect, 5);
    CHECK(answer.overBudget);
    CHECK(answer.inView == 30);
    CHECK(answer.objects.empty());
}

TEST_CASE("ProcessorWorkspaceSourceTest: ClassesIncludeAssertedLabels", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    const int stroma = classIndex(*source, kNuclei, "stroma");
    const int tumour = classIndex(*source, kNuclei, "tumour");
    REQUIRE(stroma >= 0);
    REQUIRE(tumour >= 0);

    const auto answer = source->query(kNuclei, kWholeRect, 100);
    REQUIRE(answer.objects.size() == 30);
    const auto find = [&](const std::string& id) {
        return std::find_if(answer.objects.begin(), answer.objects.end(),
                            [&](const auto& o) { return o.id == id; });
    };
    const auto stromaNucleus = find(fixture->stromaNucleusId());
    REQUIRE(stromaNucleus != answer.objects.end());
    CHECK(stromaNucleus->classes == std::vector<int>{stroma});
    const auto tumourNucleus = find(fixture->tumourNucleusId());
    REQUIRE(tumourNucleus != answer.objects.end());
    CHECK(tumourNucleus->classes == std::vector<int>{tumour});
    const auto unlabelled = std::count_if(answer.objects.begin(), answer.objects.end(),
                                          [](const auto& o) { return o.classes.empty(); });
    CHECK(unlabelled == 10);
}

TEST_CASE("ProcessorWorkspaceSourceTest: DetailsIncludeAssertedMeasurements", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    const auto details = source->details(fixture->stromaNucleusId());
    REQUIRE(details.has_value());
    CHECK(details->layer == "nuclei");
    CHECK(details->classes == std::vector<std::string>{"stroma"});
    CHECK(details->stage == "segment-nuclei");
    REQUIRE(details->measurements.size() == 2);
    CHECK(details->measurements[0].name == "area");
    CHECK(details->measurements[0].value == 100.0);
    CHECK(details->measurements[0].unit == "px\xC2\xB2");
    CHECK(details->measurements[0].assertedBy.empty());
    CHECK(details->measurements[1].name == "intensity.mean");
    CHECK(details->measurements[1].value == 128.0);
    CHECK(details->measurements[1].unit.empty());
    CHECK(details->measurements[1].assertedBy == ProcessorFixture::kClassifyStage);

    CHECK_FALSE(source->details("2:999999:999999").has_value());
    CHECK_FALSE(source->details("not an id").has_value());
}

TEST_CASE("ProcessorWorkspaceSourceTest: ScanVisitsEveryObjectAndCancels", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    int visits = 0;
    int classified = 0;
    double lastFraction = -1.0;
    source->scan(
        kNuclei,
        [&](const Rect<double>& bounds, const std::vector<int>& classes) {
            ++visits;
            CHECK(bounds.width == 10.0);
            if (!classes.empty()) {
                ++classified;
            }
        },
        [&](double fraction) {
            lastFraction = fraction;
            return true;
        });
    CHECK(visits == 30);
    CHECK(classified == 20);
    CHECK(lastFraction == 1.0);

    int cancelledVisits = 0;
    int progressCalls = 0;
    source->scan(
        kNuclei, [&](const Rect<double>&, const std::vector<int>&) { ++cancelledVisits; },
        [&](double) {
            ++progressCalls;
            return false;
        });
    CHECK(progressCalls == 1);
    CHECK(cancelledVisits < 30);
}

TEST_CASE("ProcessorWorkspaceSourceTest: ReadOnlySeesObjectsCommittedAfterOpen", "[infra][ProcessorWorkspaceSource]")
{
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    auto fixture = ProcessorFixture::create(tempPath(temporary));
    auto source = ProcessorWorkspaceSource::open(tempPath(temporary));

    CHECK(source->query(kNuclei, kWholeRect, 100).inView == 30);
    fixture->addNucleus(900, 900);
    const auto answer = source->query(kNuclei, kWholeRect, 100);
    CHECK(answer.inView == 31);
    CHECK(answer.objects.size() == 31);
}
