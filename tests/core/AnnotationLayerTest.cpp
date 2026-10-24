#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationLayer.h"

#include <cmath>
#include <string>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

Annotation on(const std::string& id, const std::string& layerId)
{
    Annotation annotation(id, PointGeometry{PointF{1.0, 2.0}});
    annotation.setLayerId(layerId);
    return annotation;
}

} // namespace

TEST_CASE("the default layers are Diagnostic, Teaching, Research, bottom to top", "[core][AnnotationLayer]")
{
    const std::vector<AnnotationLayer> layers = defaultLayers();
    REQUIRE(layers.size() == 3);
    CHECK(layers[0].id == "diagnostic");
    CHECK(layers[0].name == "Diagnostic");
    CHECK(layers[1].id == "teaching");
    CHECK(layers[1].name == "Teaching");
    CHECK(layers[2].id == "research");
    CHECK(layers[2].name == "Research");
    for (const AnnotationLayer& layer : layers) {
        CHECK(layer.visible);
        CHECK_FALSE(layer.locked);
        CHECK(layer.opacity == 1.0);
    }
}

TEST_CASE("an annotation is always on a layer the scene has", "[core][AnnotationLayer]")
{
    const std::vector<AnnotationLayer> layers = defaultLayers();
    CHECK(effectiveLayerId(on("a", ""), layers) == "diagnostic");
    CHECK(effectiveLayerId(on("a", "teaching"), layers) == "teaching");
    CHECK(effectiveLayerId(on("a", "gone"), layers) == "diagnostic");
    REQUIRE(findLayer(layers, "research") != nullptr);
    CHECK(findLayer(layers, "gone") == nullptr);
}

TEST_CASE("layers carry no information in their default state", "[core][AnnotationLayer]")
{
    CHECK_FALSE(layersCarryInformation(defaultLayers(), {on("a", ""), on("b", "diagnostic")}));
}

TEST_CASE("any single deviation from the defaults carries information", "[core][AnnotationLayer]")
{
    const std::vector<Annotation> plain = {on("a", "")};
    CHECK(layersCarryInformation(defaultLayers(), {on("a", "teaching")}));

    auto changed = [](auto edit) {
        std::vector<AnnotationLayer> layers = defaultLayers();
        edit(layers);
        return layers;
    };
    CHECK(layersCarryInformation(changed([](auto& l) { l[1].visible = false; }), plain));
    CHECK(layersCarryInformation(changed([](auto& l) { l[2].locked = true; }), plain));
    CHECK(layersCarryInformation(changed([](auto& l) { l[0].opacity = 0.5; }), plain));
    CHECK(layersCarryInformation(changed([](auto& l) { l[1].name = "Students"; }), plain));
    CHECK(layersCarryInformation(changed([](auto& l) { std::swap(l[0], l[2]); }), plain));
    CHECK(layersCarryInformation(changed([](auto& l) { l.push_back(AnnotationLayer{"x", "Extra"}); }), plain));
    CHECK(layersCarryInformation(changed([](auto& l) { l.pop_back(); }), plain));
}

TEST_CASE("a layer list must have unique ids, names and a sane opacity", "[core][AnnotationLayer]")
{
    std::string problem;
    CHECK(isValidLayerList(defaultLayers(), problem));
    CHECK_FALSE(isValidLayerList({}, problem));
    CHECK_FALSE(isValidLayerList({AnnotationLayer{"a", "A"}, AnnotationLayer{"a", "B"}}, problem));
    CHECK(problem.find("'a'") != std::string::npos);
    CHECK_FALSE(isValidLayerList({AnnotationLayer{"", "A"}}, problem));
    CHECK_FALSE(isValidLayerList({AnnotationLayer{"a", ""}}, problem));
    AnnotationLayer tooOpaque{"a", "A"};
    tooOpaque.opacity = 1.5;
    CHECK_FALSE(isValidLayerList({tooOpaque}, problem));
    AnnotationLayer notANumber{"a", "A"};
    notANumber.opacity = std::nan("");
    CHECK_FALSE(isValidLayerList({notANumber}, problem));
}

TEST_CASE("paint order is bottom layer first, model order within a layer", "[core][AnnotationLayer]")
{
    const std::vector<AnnotationLayer> layers = {AnnotationLayer{"d", "D"}, AnnotationLayer{"t", "T"}};
    const std::vector<Annotation> annotations = {on("t0", "t"), on("d1", "d"), on("t2", "t"), on("d3", "d")};
    CHECK(paintOrder(annotations, layers) == std::vector<std::size_t>{1, 3, 0, 2});
}

TEST_CASE("merging layer lists keeps ours and adds theirs on top", "[core][AnnotationLayer]")
{
    std::vector<AnnotationLayer> ours = defaultLayers();
    ours[1].visible = false;
    std::vector<AnnotationLayer> theirs = defaultLayers();
    theirs[1].visible = true;
    theirs[2].name = "Renamed there";
    theirs.push_back(AnnotationLayer{"students", "Students"});

    const std::vector<AnnotationLayer> merged = mergeLayerLists(ours, theirs);

    REQUIRE(merged.size() == 4);
    // Ours win on a shared id: what the user was looking at is what they keep.
    CHECK_FALSE(merged[1].visible);
    CHECK(merged[2].name == "Research");
    CHECK(merged[3] == AnnotationLayer{"students", "Students"});
}

TEST_CASE("merging with a file that said nothing about layers keeps ours", "[core][AnnotationLayer]")
{
    std::vector<AnnotationLayer> ours = {AnnotationLayer{"only", "Only"}};
    // An empty list is a version-1 file: the defaults, which then add their three.
    CHECK(mergeLayerLists(ours, {}).size() == 4);
    CHECK(mergeLayerLists(ours, {}).front() == ours.front());
    CHECK(mergeLayerLists(defaultLayers(), {}) == defaultLayers());
}
