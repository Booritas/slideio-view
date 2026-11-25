#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/infra/QSettingsResultsSettingsStore.h"

#include <QString>
#include <QTemporaryDir>

using namespace slideio::viewer::core;
using slideio::viewer::infra::QSettingsResultsSettingsStore;

namespace
{
const std::string kSlide = "a3f8c2e109b74d21";

ResultsAttachmentSettings makeEntry()
{
    ResultsAttachmentSettings e;
    e.slideId = kSlide;
    e.scene = 2;
    e.workspacePath = "D:/work/ws one";
    e.attachAnywaySlidePath = "E:/moved/case.svs";
    ResultsLayerView a;
    a.layerName = "nuclei";
    a.visible = false;
    a.colour = Color{10, 20, 30, 255};
    a.fill = true;
    a.fillOpacity = 0.55;
    a.colourByClass = false;
    a.hiddenClasses = {"Tumour, grade 2", "a/b", "plain"};
    ResultsLayerView b;
    b.layerName = "tissue/regions";
    e.layers = {a, b};
    return e;
}
} // namespace

TEST_CASE("a full attachment entry round-trips", "[infra][ResultsSettingsStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("r.ini").toStdString();
    {
        QSettingsResultsSettingsStore store(ini);
        REQUIRE(store.set(makeEntry()));
    }
    QSettingsResultsSettingsStore reopened(ini);
    const auto found = reopened.find(kSlide, 2);
    REQUIRE(found.has_value());
    CHECK(found->workspacePath == "D:/work/ws one");
    CHECK(found->attachAnywaySlidePath == "E:/moved/case.svs");
    REQUIRE(found->layers.size() == 2);
    const auto& a = found->layers[0];
    CHECK(a.layerName == "nuclei");
    CHECK_FALSE(a.visible);
    REQUIRE(a.colour.has_value());
    CHECK(a.colour->r == 10);
    CHECK(a.colour->g == 20);
    CHECK(a.colour->b == 30);
    CHECK(a.fill);
    CHECK(a.fillOpacity == 0.55);
    CHECK_FALSE(a.colourByClass);
    CHECK(a.hiddenClasses == std::vector<std::string>{"Tumour, grade 2", "a/b", "plain"});
    const auto& b = found->layers[1];
    CHECK(b.layerName == "tissue/regions");
    CHECK(b.visible);
    CHECK_FALSE(b.colour.has_value());
    CHECK(b.colourByClass);
    CHECK(b.hiddenClasses.empty());
    CHECK_FALSE(reopened.find(kSlide, 3).has_value());
}

TEST_CASE("remove drops the entry", "[infra][ResultsSettingsStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsResultsSettingsStore store(dir.filePath("r.ini").toStdString());
    REQUIRE(store.set(makeEntry()));
    store.remove(kSlide, 2);
    CHECK_FALSE(store.find(kSlide, 2).has_value());
}

TEST_CASE("class colours are keyed case-insensitively", "[infra][ResultsSettingsStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("r.ini").toStdString();
    {
        QSettingsResultsSettingsStore store(ini);
        store.setClassColour("Tumour", Color{1, 2, 3, 255});
        store.setClassColour("a/b", Color{4, 5, 6, 255});
    }
    QSettingsResultsSettingsStore reopened(ini);
    const auto colours = reopened.classColours();
    const auto it = colours.find(caseFoldKey("TUMOUR"));
    REQUIRE(it != colours.end());
    CHECK(it->second.r == 1);
    CHECK(it->second.b == 3);
    const auto slash = colours.find(caseFoldKey("A/B"));
    REQUIRE(slash != colours.end());
    CHECK(slash->second.g == 5);
}
