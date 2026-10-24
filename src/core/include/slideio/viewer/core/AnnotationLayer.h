#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <cstddef>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// A named group of a scene's annotations (FR-ANN-23): Diagnostic marks, a
/// teaching layer hidden until the answers are revealed, research traces kept
/// out of a sign-out. A scene's layers are an ordered list whose index 0 is the
/// BOTTOM -- order is z-order: a higher layer paints on top and is hit first.
struct AnnotationLayer
{
    std::string id;
    std::string name;
    bool visible = true;
    bool locked = false;
    /// Multiplies every annotation's stroke and fill alpha; never stored in
    /// the annotations themselves. [0, 1].
    double opacity = 1.0;

    bool operator==(const AnnotationLayer& other) const;
    bool operator!=(const AnnotationLayer& other) const { return !(*this == other); }
};

/// Diagnostic, Teaching, Research, bottom to top, all visible, unlocked and
/// opaque (FR-ANN-24). Their ids are FIXED ("diagnostic", "teaching",
/// "research"): that is what lets every file written before layers -- whose
/// annotations name no layer -- land on Diagnostic with no migration.
[[nodiscard]] std::vector<AnnotationLayer> defaultLayers();

/// The layer `id` names in `layers`, or null.
[[nodiscard]] const AnnotationLayer* findLayer(const std::vector<AnnotationLayer>& layers, const std::string& id);

/// The layer an annotation is on: its own layerId when the scene has that
/// layer, otherwise the BOTTOM layer's. An empty layerId -- every annotation
/// saved before layers -- is the bottom layer, Diagnostic by default. Every
/// reader of an annotation's layer goes through this, which is what keeps
/// "never on no layer" true for old files, hand edits, imports and undo after
/// a layer was deleted. `layers` must not be empty.
[[nodiscard]] std::string effectiveLayerId(const Annotation& annotation, const std::vector<AnnotationLayer>& layers);

/// Whether a scene's layers say anything a file without them would lose: an
/// annotation off the bottom layer, or a layer list that is not exactly
/// defaultLayers() -- renamed, added, deleted, reordered, hidden, locked or
/// faded. The serializer writes schemaVersion 2 when this is true and the
/// pre-layer format otherwise, so an older build REFUSES a file whose layers
/// mean something instead of rewriting it without them.
[[nodiscard]] bool layersCarryInformation(const std::vector<AnnotationLayer>& layers,
                                          const std::vector<Annotation>& annotations);

/// Non-empty, with unique non-empty ids, non-empty names and a finite opacity
/// in [0, 1]. `problem` names what is wrong, for the user.
[[nodiscard]] bool isValidLayerList(const std::vector<AnnotationLayer>& layers, std::string& problem);

/// The layers of two copies of one scene merged: `ours` as they are, then each
/// of `theirs` whose id `ours` lacks, on top in their own order. Ours win a
/// shared id, because they are what the user is looking at. An empty `theirs`
/// is a file that said nothing about layers, so it adds none: its annotations
/// name no layer and land on our bottom one, and the default layers the user
/// deleted here do not come back.
[[nodiscard]] std::vector<AnnotationLayer> mergeLayerLists(const std::vector<AnnotationLayer>& ours,
                                                           const std::vector<AnnotationLayer>& theirs);

/// Indices into `annotations` in the order they paint: bottom layer first,
/// model order within a layer. Hit-testing searches it in reverse, so the
/// shape painted on top is the shape a click finds.
[[nodiscard]] std::vector<std::size_t> paintOrder(const std::vector<Annotation>& annotations,
                                                  const std::vector<AnnotationLayer>& layers);

} // namespace slideio::viewer::core
