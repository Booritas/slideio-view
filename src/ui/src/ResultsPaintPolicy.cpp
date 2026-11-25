#include "slideio/viewer/ui/ResultsPaintPolicy.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace slideio::viewer::ui
{

namespace
{

/// Matches app::ResultsOverlayService::kUnclassifiedName.
const std::string kUnclassifiedName;

bool hidden(const core::ResultsLayerView& view, const std::string& name)
{
    return std::find(view.hiddenClasses.begin(), view.hiddenClasses.end(), name) != view.hiddenClasses.end();
}

bool listed(int classIndex, const std::vector<core::Color>& classColours)
{
    return classIndex >= 0 && static_cast<std::size_t>(classIndex) < classColours.size();
}

} // namespace

core::Color objectColour(const core::ResultObject& object, const core::ResultsLayerView& view,
                         const std::vector<core::Color>& classColours, core::Color layerColour)
{
    if (view.colourByClass && !object.classes.empty() && listed(object.classes.front(), classColours)) {
        return classColours[static_cast<std::size_t>(object.classes.front())];
    }
    return layerColour;
}

core::Color overviewColour(int dominant, const std::vector<core::Color>& classColours, core::Color layerColour)
{
    if (dominant == core::OverviewGrid::kOtherClasses) {
        return core::Color{0x9E, 0x9E, 0x9E, 255};
    }
    if (listed(dominant, classColours)) {
        return classColours[static_cast<std::size_t>(dominant)];
    }
    return layerColour;
}

ClassVisibility classVisibility(const core::ResultLayerInfo& layer, const core::ResultsLayerView& view)
{
    ClassVisibility visibility;
    visibility.classVisible.reserve(layer.classes.size());
    for (const core::ResultClassInfo& info : layer.classes) {
        visibility.classVisible.push_back(!hidden(view, info.name));
    }
    visibility.unclassifiedVisible = !hidden(view, kUnclassifiedName);
    return visibility;
}

core::Rect<double> grownView(const core::Rect<double>& view)
{
    return core::Rect<double>{view.x - view.width / 2.0, view.y - view.height / 2.0, view.width * 2.0,
                              view.height * 2.0};
}

bool rectContains(const core::Rect<double>& outer, const core::Rect<double>& inner)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width
        && inner.y + inner.height <= outer.y + outer.height;
}

core::Rect<double> overviewCellRect(const core::OverviewGrid& grid, const core::OverviewGrid::Cell& cell)
{
    const double size = grid.squareSize();
    return core::Rect<double>{grid.sceneRect().x + cell.column * size, grid.sceneRect().y + cell.row * size, size,
                              size};
}

} // namespace slideio::viewer::ui
