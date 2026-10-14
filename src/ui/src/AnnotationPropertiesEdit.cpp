#include "slideio/viewer/ui/AnnotationPropertiesEdit.h"

#include <algorithm>

namespace slideio::viewer::ui
{

namespace
{

bool sameColor(const core::Color& a, const core::Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

} // namespace

std::optional<core::AnnotationProperties> propertiesEdit(
    const core::AnnotationProperties& current, const AnnotationFieldValues& fields)
{
    core::AnnotationProperties edited;
    edited.label = fields.label;
    edited.classification = fields.classification;
    edited.notes = fields.notes;
    edited.color = fields.color;
    edited.lineWidth = std::max(1.0f, std::min(fields.lineWidth, 5.0f));
    edited.fillOpacity = std::max(0.0f, std::min(fields.fillOpacity, 1.0f));

    const bool unchanged = edited.label == current.label
                           && edited.classification == current.classification
                           && edited.notes == current.notes
                           && sameColor(edited.color, current.color)
                           && edited.lineWidth == current.lineWidth
                           && edited.fillOpacity == current.fillOpacity;
    if (unchanged) {
        return std::nullopt;
    }
    return edited;
}

std::string propertiesEditDescription(const core::AnnotationProperties& before,
                                      const core::AnnotationProperties& after)
{
    const bool label = before.label != after.label;
    const bool classification = before.classification != after.classification;
    const bool color = !sameColor(before.color, after.color);
    const bool notes = before.notes != after.notes;
    const bool style = before.lineWidth != after.lineWidth
                       || before.fillOpacity != after.fillOpacity;

    const int changed = static_cast<int>(label) + static_cast<int>(classification)
                        + static_cast<int>(color) + static_cast<int>(notes)
                        + static_cast<int>(style);
    if (changed != 1) {
        return "Edit annotation";
    }
    if (label) {
        return "Rename annotation";
    }
    if (classification) {
        return "Reclassify annotation";
    }
    if (color) {
        return "Recolour annotation";
    }
    if (notes) {
        return "Edit annotation notes";
    }
    return "Restyle annotation";
}

} // namespace slideio::viewer::ui
