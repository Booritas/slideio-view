#include "slideio/viewer/core/AnnotationLayer.h"

#include <cmath>
#include <set>

namespace slideio::viewer::core
{

bool AnnotationLayer::operator==(const AnnotationLayer& other) const
{
    return id == other.id && name == other.name && visible == other.visible && locked == other.locked
           && opacity == other.opacity;
}

std::vector<AnnotationLayer> defaultLayers()
{
    return {AnnotationLayer{"diagnostic", "Diagnostic"}, AnnotationLayer{"teaching", "Teaching"},
            AnnotationLayer{"research", "Research"}};
}

const AnnotationLayer* findLayer(const std::vector<AnnotationLayer>& layers, const std::string& id)
{
    for (const AnnotationLayer& layer : layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

std::string effectiveLayerId(const Annotation& annotation, const std::vector<AnnotationLayer>& layers)
{
    if (layers.empty()) {
        return annotation.layerId();
    }
    return findLayer(layers, annotation.layerId()) != nullptr ? annotation.layerId() : layers.front().id;
}

bool layersCarryInformation(const std::vector<AnnotationLayer>& layers, const std::vector<Annotation>& annotations)
{
    if (layers != defaultLayers()) {
        return true;
    }
    for (const Annotation& annotation : annotations) {
        if (effectiveLayerId(annotation, layers) != layers.front().id) {
            return true;
        }
    }
    return false;
}

bool isValidLayerList(const std::vector<AnnotationLayer>& layers, std::string& problem)
{
    if (layers.empty()) {
        problem = "layers is empty: a scene needs at least one layer";
        return false;
    }
    std::set<std::string> ids;
    for (const AnnotationLayer& layer : layers) {
        if (layer.id.empty()) {
            problem = "a layer has an empty id";
            return false;
        }
        if (!ids.insert(layer.id).second) {
            problem = "two layers share the id '" + layer.id + "'";
            return false;
        }
        if (layer.name.empty()) {
            problem = "layer '" + layer.id + "' has an empty name";
            return false;
        }
        if (!std::isfinite(layer.opacity) || layer.opacity < 0.0 || layer.opacity > 1.0) {
            problem = "layer '" + layer.id + "' has an opacity outside 0 to 1";
            return false;
        }
    }
    return true;
}

std::vector<std::size_t> paintOrder(const std::vector<Annotation>& annotations,
                                    const std::vector<AnnotationLayer>& layers)
{
    std::vector<std::size_t> order;
    order.reserve(annotations.size());
    for (const AnnotationLayer& layer : layers) {
        for (std::size_t i = 0; i < annotations.size(); ++i) {
            if (effectiveLayerId(annotations[i], layers) == layer.id) {
                order.push_back(i);
            }
        }
    }
    return order;
}

std::vector<AnnotationLayer> mergeLayerLists(const std::vector<AnnotationLayer>& ours,
                                             const std::vector<AnnotationLayer>& theirs)
{
    std::vector<AnnotationLayer> merged = ours;
    for (const AnnotationLayer& layer : theirs) {
        if (findLayer(merged, layer.id) == nullptr) {
            merged.push_back(layer);
        }
    }
    return merged;
}

} // namespace slideio::viewer::core
