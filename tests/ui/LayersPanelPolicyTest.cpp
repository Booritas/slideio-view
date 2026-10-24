#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/LayersPanelPolicy.h"

#include <string>
#include <vector>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;

namespace
{

core::Annotation on(const std::string& id, const std::string& layerId)
{
    core::Annotation annotation(id, core::PointGeometry{core::PointF{1.0, 1.0}});
    annotation.setLayerId(layerId);
    return annotation;
}

} // namespace

TEST_CASE("layer rows are listed top first with their counts", "[ui][LayersPanelPolicy]")
{
    const std::vector<core::Annotation> annotations = {on("a", ""), on("b", "teaching"), on("c", "gone")};
    const std::vector<LayerRow> rows = layerRows(core::defaultLayers(), annotations, "teaching");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].id == "research");
    CHECK(rows[1].id == "teaching");
    CHECK(rows[2].id == "diagnostic");
    CHECK(rows[0].count == 0);
    CHECK(rows[1].count == 1);
    // "" and the dangling "gone" both resolve to the bottom layer.
    CHECK(rows[2].count == 2);
    CHECK(rows[1].active);
    CHECK_FALSE(rows[2].active);
}

TEST_CASE("only an empty layer that is not the last can be deleted", "[ui][LayersPanelPolicy]")
{
    const std::vector<core::Annotation> annotations = {on("a", "teaching")};
    CHECK_FALSE(canDeleteLayer(core::defaultLayers(), annotations, "teaching"));
    CHECK(canDeleteLayer(core::defaultLayers(), annotations, "research"));
    CHECK_FALSE(canDeleteLayer({core::AnnotationLayer{"only", "Only"}}, {}, "only"));
    CHECK_FALSE(canDeleteLayer(core::defaultLayers(), annotations, "missing"));
}

TEST_CASE("merge targets are every other layer, top first", "[ui][LayersPanelPolicy]")
{
    CHECK(mergeTargets(core::defaultLayers(), "teaching") == std::vector<std::string>{"research", "diagnostic"});
}

TEST_CASE("a layer moves up towards the top and down towards the bottom", "[ui][LayersPanelPolicy]")
{
    const std::vector<core::AnnotationLayer> layers = core::defaultLayers();
    CHECK(canMoveLayerUp(layers, "diagnostic"));
    CHECK_FALSE(canMoveLayerUp(layers, "research"));
    CHECK(canMoveLayerDown(layers, "research"));
    CHECK_FALSE(canMoveLayerDown(layers, "diagnostic"));

    const std::vector<core::AnnotationLayer> up = withLayerMoved(layers, "diagnostic", +1);
    REQUIRE(up.size() == 3);
    CHECK(up[0].id == "teaching");
    CHECK(up[1].id == "diagnostic");
    CHECK(withLayerMoved(layers, "research", +1) == layers);
}

TEST_CASE("a hidden or locked layer refuses with its reason", "[ui][LayersPanelPolicy]")
{
    core::AnnotationLayer layer{"teaching", "Teaching"};
    CHECK(layerRefusal(layer).empty());
    layer.locked = true;
    CHECK(layerRefusal(layer) == "Layer Teaching is locked");
    layer.visible = false;
    CHECK(layerRefusal(layer) == "Layer Teaching is hidden");
}

TEST_CASE("a click on a row's eye or padlock does not make it the active layer", "[ui][LayersPanelPolicy]")
{
    // Hiding Teaching to keep drawing on Diagnostic must not move the drawing
    // onto Teaching -- where it would then be refused as hidden.
    CHECK_FALSE(columnActivatesLayer(LayerColumn::Visible));
    CHECK_FALSE(columnActivatesLayer(LayerColumn::Locked));
    CHECK(columnActivatesLayer(LayerColumn::Name));
    CHECK(columnActivatesLayer(LayerColumn::Count));
    CHECK(columnActivatesLayer(LayerColumn::Opacity));
}
