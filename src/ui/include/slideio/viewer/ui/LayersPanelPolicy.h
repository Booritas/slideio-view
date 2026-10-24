#pragma once

#include "slideio/viewer/core/AnnotationLayer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace slideio::viewer::ui
{

/// One row of the Layers tab.
struct LayerRow
{
    std::string id;
    std::string name;
    std::size_t count = 0;
    bool visible = true;
    bool locked = false;
    double opacity = 1.0;
    bool active = false;
};

/// The Layers tab's rows, TOP layer first -- the order a user reads a stack in,
/// the reverse of the model's bottom-first list. Counts go through
/// core::effectiveLayerId, so an annotation naming a deleted layer is counted
/// on the bottom one, where it is shown.
[[nodiscard]] std::vector<LayerRow> layerRows(const std::vector<core::AnnotationLayer>& layers,
                                              const std::vector<core::Annotation>& annotations,
                                              const std::string& activeLayerId);

/// Only an empty layer, and never the last: deleting a layer that holds
/// annotations would leave an undo able to put them back on a layer that no
/// longer exists. Merge empties a layer first, as one undo step.
[[nodiscard]] bool canDeleteLayer(const std::vector<core::AnnotationLayer>& layers,
                                  const std::vector<core::Annotation>& annotations, const std::string& id);

/// The layers `sourceId` can be merged into: every other one, top first.
[[nodiscard]] std::vector<std::string> mergeTargets(const std::vector<core::AnnotationLayer>& layers,
                                                    const std::string& sourceId);

/// "Up" is towards the TOP of the stack -- a higher index, painted later.
[[nodiscard]] bool canMoveLayerUp(const std::vector<core::AnnotationLayer>& layers, const std::string& id);
[[nodiscard]] bool canMoveLayerDown(const std::vector<core::AnnotationLayer>& layers, const std::string& id);

/// `layers` with `id` moved `delta` places (+1 up, -1 down); unchanged when it
/// cannot move that far or is not there.
[[nodiscard]] std::vector<core::AnnotationLayer> withLayerMoved(const std::vector<core::AnnotationLayer>& layers,
                                                                const std::string& id, int delta);

/// Why nothing may be drawn on, imported into or changed on `layer`: "Layer
/// <name> is hidden" (checked first: a hidden layer cannot even be seen),
/// "Layer <name> is locked", or empty when it is open.
[[nodiscard]] std::string layerRefusal(const core::AnnotationLayer& layer);

} // namespace slideio::viewer::ui
