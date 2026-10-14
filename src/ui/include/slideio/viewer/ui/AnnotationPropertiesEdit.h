#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <array>
#include <optional>
#include <string>

namespace slideio::viewer::ui
{

/// Offered in the classification combo, which stays editable: these are
/// suggestions, not a controlled vocabulary. classification remains a plain
/// string in the stored file, so the serializer and schemaVersion are untouched
/// by what is listed here.
inline constexpr std::array<const char*, 5> kAnnotationClassifications{
    "Tumor", "Stroma", "Necrosis", "Normal", "Artifact"};

/// What the properties panel's widgets currently hold.
struct AnnotationFieldValues
{
    std::string label;
    std::string classification;
    std::string notes;
    core::Color color;
    float lineWidth = 2.0f;
    float fillOpacity = 0.3f;
};

/// The properties to store, or nullopt when `fields` would change nothing.
///
/// Returning nullopt for a no-op is what keeps tabbing through an untouched
/// field from pushing an undo step, restamping modifiedAt and triggering an
/// autosave that writes a modification nobody made.
///
/// lineWidth is clamped to 1-5 (FR-ANN-11) and fillOpacity to 0-1. The clamp
/// applies to values arriving from the model as well as from the widgets: the
/// model was loaded from a file a person can edit by hand, and a lineWidth of 0
/// draws nothing at all. A clamp counts as a change, so an out-of-range value
/// is corrected and persisted once rather than re-clamped on every load.
[[nodiscard]] std::optional<core::AnnotationProperties> propertiesEdit(
    const core::AnnotationProperties& current, const AnnotationFieldValues& fields);

/// What the Edit menu should call this change: "Rename annotation",
/// "Reclassify annotation", "Recolour annotation", "Edit annotation notes",
/// "Restyle annotation", or "Edit annotation" when more than one field moved.
///
/// Note: these strings are intentionally not wrapped in tr() because this file
/// must remain Qt-free to be testable. If .ts files arrive, translating these
/// strings belongs in the panel implementation, not here.
[[nodiscard]] std::string propertiesEditDescription(const core::AnnotationProperties& before,
                                                    const core::AnnotationProperties& after);

} // namespace slideio::viewer::ui
