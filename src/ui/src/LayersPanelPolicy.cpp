#include "slideio/viewer/ui/LayersPanelPolicy.h"

#include <algorithm>

namespace slideio::viewer::ui
{

namespace
{

/// Index of `id` in `layers`, or -1.
int indexOf(const std::vector<core::AnnotationLayer>& layers, const std::string& id)
{
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

std::vector<LayerRow> layerRows(const std::vector<core::AnnotationLayer>& layers,
                                const std::vector<core::Annotation>& annotations, const std::string& activeLayerId)
{
    std::vector<LayerRow> rows;
    rows.reserve(layers.size());
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        LayerRow row;
        row.id = it->id;
        row.name = it->name;
        row.visible = it->visible;
        row.locked = it->locked;
        row.opacity = it->opacity;
        row.active = it->id == activeLayerId;
        for (const core::Annotation& annotation : annotations) {
            if (core::effectiveLayerId(annotation, layers) == it->id) {
                ++row.count;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool canDeleteLayer(const std::vector<core::AnnotationLayer>& layers, const std::vector<core::Annotation>& annotations,
                    const std::string& id)
{
    if (layers.size() <= 1 || indexOf(layers, id) < 0) {
        return false;
    }
    return std::none_of(annotations.begin(), annotations.end(), [&](const core::Annotation& annotation) {
        return core::effectiveLayerId(annotation, layers) == id;
    });
}

std::vector<std::string> mergeTargets(const std::vector<core::AnnotationLayer>& layers, const std::string& sourceId)
{
    std::vector<std::string> targets;
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (it->id != sourceId) {
            targets.push_back(it->id);
        }
    }
    return targets;
}

bool canMoveLayerUp(const std::vector<core::AnnotationLayer>& layers, const std::string& id)
{
    const int index = indexOf(layers, id);
    return index >= 0 && index + 1 < static_cast<int>(layers.size());
}

bool canMoveLayerDown(const std::vector<core::AnnotationLayer>& layers, const std::string& id)
{
    return indexOf(layers, id) > 0;
}

std::vector<core::AnnotationLayer> withLayerMoved(const std::vector<core::AnnotationLayer>& layers,
                                                  const std::string& id, int delta)
{
    const int from = indexOf(layers, id);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= static_cast<int>(layers.size())) {
        return layers;
    }
    std::vector<core::AnnotationLayer> moved = layers;
    std::swap(moved[static_cast<std::size_t>(from)], moved[static_cast<std::size_t>(to)]);
    return moved;
}

std::string layerRefusal(const core::AnnotationLayer& layer)
{
    if (!layer.visible) {
        return "Layer " + layer.name + " is hidden";
    }
    if (layer.locked) {
        return "Layer " + layer.name + " is locked";
    }
    return {};
}

} // namespace slideio::viewer::ui
