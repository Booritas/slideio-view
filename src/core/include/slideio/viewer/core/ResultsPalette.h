#pragma once

#include "slideio/viewer/core/Types.h"

#include <map>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// FR-ANN-15's colourblind-safe colours first (blue, orange, yellow, purple,
/// cyan), then further distinct colours.
[[nodiscard]] const std::vector<Color>& resultsPalette();

/// The override for `className` (keyed by caseFoldKey) if present, else the
/// palette entry for `legendIndex`, cycling.
[[nodiscard]] Color resultClassColour(const std::string& className, int legendIndex,
                                      const std::map<std::string, Color>& overridesByFoldedName);

/// Colour of a layer's outline when it is not split by class.
[[nodiscard]] Color resultLayerColour(int layerIndex);

} // namespace slideio::viewer::core
