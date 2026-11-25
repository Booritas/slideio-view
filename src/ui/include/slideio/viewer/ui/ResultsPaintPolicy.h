#pragma once

#include "slideio/viewer/core/OverviewGrid.h"
#include "slideio/viewer/core/ResultsAttachment.h"
#include "slideio/viewer/core/ResultsSource.h"
#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <vector>

namespace slideio::viewer::ui
{

/// Width of a result's outline, in screen pixels.
constexpr double kResultOutlineScreenPixels = 1.5;
/// Width of the selected result's outline, in screen pixels.
constexpr double kResultSelectedOutlineScreenPixels = 2.5;
/// Ring points closer than this on screen are dropped before drawing.
constexpr double kResultThinningScreenPixels = 0.5;

/// The colour `object` is drawn in: its first class's colour when the layer is
/// coloured by class and the object has a class the palette lists, else the
/// layer colour. classColours is the layer's classes in legend order.
[[nodiscard]] core::Color objectColour(const core::ResultObject& object, const core::ResultsLayerView& view,
                                       const std::vector<core::Color>& classColours, core::Color layerColour);

/// The colour of an overview square whose dominant bucket is `dominant`: a class
/// index, core::OverviewGrid::kOtherClasses (grey) or kUnclassified (the layer
/// colour). An index the palette does not list takes the layer colour.
[[nodiscard]] core::Color overviewColour(int dominant, const std::vector<core::Color>& classColours,
                                         core::Color layerColour);

/// What core::OverviewGrid::cells() takes for a layer's legend settings.
struct ClassVisibility
{
    std::vector<bool> classVisible;   ///< indexed by class index
    bool unclassifiedVisible = true;

    bool operator==(const ClassVisibility& other) const
    {
        return classVisible == other.classVisible && unclassifiedVisible == other.unclassifiedVisible;
    }
    bool operator!=(const ClassVisibility& other) const { return !(*this == other); }
};
/// A class is hidden when its name is in view.hiddenClasses; Unclassified when
/// the empty name (app::ResultsOverlayService::kUnclassifiedName) is.
[[nodiscard]] ClassVisibility classVisibility(const core::ResultLayerInfo& layer,
                                              const core::ResultsLayerView& view);

/// The level-0 rectangle an overview cell covers.
[[nodiscard]] core::Rect<double> overviewCellRect(const core::OverviewGrid& grid,
                                                  const core::OverviewGrid::Cell& cell);

/// A layer's overview as an image, one pixel per grid square: columns() wide,
/// rows() high, row-major, each pixel a premultiplied 0xAARRGGBB value (what
/// QImage::Format_ARGB32_Premultiplied holds). A cell's pixel is
/// overviewColour(...) with its alpha scaled by the cell's opacity; with
/// colour-by-class off every cell takes the layer colour. Squares without a
/// cell stay transparent.
[[nodiscard]] std::vector<uint32_t> overviewPixels(const core::OverviewGrid& grid,
                                                   const std::vector<core::OverviewGrid::Cell>& cells,
                                                   bool colourByClass,
                                                   const std::vector<core::Color>& classColours,
                                                   core::Color layerColour);

/// Whole grid squares, in columns and rows.
struct CellSpan
{
    int column = 0;
    int row = 0;
    int columns = 0;
    int rows = 0;

    [[nodiscard]] bool empty() const { return columns <= 0 || rows <= 0; }
};
/// The squares of `grid` that `view` touches, widened to whole squares and
/// clipped to the grid; empty when the view misses the scene.
[[nodiscard]] CellSpan overviewSpan(const core::OverviewGrid& grid, const core::Rect<double>& view);

} // namespace slideio::viewer::ui
