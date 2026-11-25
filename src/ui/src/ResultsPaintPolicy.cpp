#include "slideio/viewer/ui/ResultsPaintPolicy.h"

#include <algorithm>
#include <cmath>
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

core::Rect<double> overviewCellRect(const core::OverviewGrid& grid, const core::OverviewGrid::Cell& cell)
{
    const double size = grid.squareSize();
    return core::Rect<double>{grid.sceneRect().x + cell.column * size, grid.sceneRect().y + cell.row * size, size,
                              size};
}

std::vector<uint32_t> overviewPixels(const core::OverviewGrid& grid,
                                     const std::vector<core::OverviewGrid::Cell>& cells, bool colourByClass,
                                     const std::vector<core::Color>& classColours, core::Color layerColour)
{
    const auto columns = static_cast<std::size_t>(grid.columns());
    std::vector<uint32_t> pixels(columns * static_cast<std::size_t>(grid.rows()), 0u);
    for (const core::OverviewGrid::Cell& cell : cells) {
        if (cell.column < 0 || cell.column >= grid.columns() || cell.row < 0 || cell.row >= grid.rows()) {
            continue;
        }
        const core::Color colour =
            colourByClass ? overviewColour(cell.dominant, classColours, layerColour) : layerColour;
        const auto alpha = static_cast<uint32_t>(std::lround(colour.a * std::clamp(cell.opacity, 0.0, 1.0)));
        const auto premultiply = [alpha](uint8_t channel) {
            return static_cast<uint32_t>(std::lround(static_cast<double>(channel) * alpha / 255.0));
        };
        pixels[static_cast<std::size_t>(cell.row) * columns + static_cast<std::size_t>(cell.column)] =
            (alpha << 24) | (premultiply(colour.r) << 16) | (premultiply(colour.g) << 8)
            | premultiply(colour.b);
    }
    return pixels;
}

CellSpan overviewSpan(const core::OverviewGrid& grid, const core::Rect<double>& view)
{
    const double size = grid.squareSize();
    const core::Rect<double>& scene = grid.sceneRect();
    // Clamped as doubles first: a far-off view must not overflow an int.
    const auto bound = [](double value, int limit) {
        return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(limit)));
    };
    const int column0 = bound(std::floor((view.x - scene.x) / size), grid.columns());
    const int column1 = bound(std::ceil((view.x + view.width - scene.x) / size), grid.columns());
    const int row0 = bound(std::floor((view.y - scene.y) / size), grid.rows());
    const int row1 = bound(std::ceil((view.y + view.height - scene.y) / size), grid.rows());
    CellSpan span;
    span.column = column0;
    span.row = row0;
    span.columns = std::max(0, column1 - column0);
    span.rows = std::max(0, row1 - row0);
    return span;
}

} // namespace slideio::viewer::ui
