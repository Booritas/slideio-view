#pragma once

#include "slideio/viewer/core/OverviewGrid.h"
#include "slideio/viewer/core/ResultsAttachment.h"
#include "slideio/viewer/core/ResultsSource.h"
#include "slideio/viewer/core/Types.h"

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

/// `view` grown by half its width and height on each side: the rectangle the
/// overview cells are fetched for, so small pans reuse them.
[[nodiscard]] core::Rect<double> grownView(const core::Rect<double>& view);
/// True when `inner` lies wholly inside `outer`.
[[nodiscard]] bool rectContains(const core::Rect<double>& outer, const core::Rect<double>& inner);

/// The level-0 rectangle an overview cell covers.
[[nodiscard]] core::Rect<double> overviewCellRect(const core::OverviewGrid& grid,
                                                  const core::OverviewGrid::Cell& cell);

} // namespace slideio::viewer::ui
