#pragma once

#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <optional>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// Where a scene's pixel size came from. Shown beside every physical number
/// the viewer displays or files, because a measurement resting on a value the
/// user typed -- or on nothing -- must never look like one resting on the
/// scanner's metadata.
enum class CalibrationSource
{
    Metadata,
    Manual,
    None,
};

/// The pixel size every surface measures with, and where it came from. One
/// value per open scene, computed once and handed to the scale bar, the
/// properties panel, the Slide Properties panel and both exports, so they
/// cannot disagree.
struct Calibration
{
    /// Microns per level-0 pixel. {0, 0} when `source` is None -- deliberately
    /// not PixelSize's own {1, 1} default, which would read as calibrated.
    PixelSize micronsPerPixel{0.0, 0.0};
    CalibrationSource source = CalibrationSource::None;
};

/// The range a manual override must fall in, bounds included. A typo guard,
/// not physics: real scans run about 0.1 - 2 um/px, and a value outside the
/// range is far likelier to be mm or nm typed as um.
inline constexpr double kMinCalibrationMicronsPerPixel = 0.01;
inline constexpr double kMaxCalibrationMicronsPerPixel = 100.0;

/// Both components finite and within the range.
[[nodiscard]] bool isValidCalibrationOverride(PixelSize micronsPerPixel);

/// The calibration a scene measures with. `metadataXMetres`/`metadataYMetres`
/// are SlideInfo::resolutionX/Y, in metres. A valid override wins over the
/// metadata -- that is what "the metadata is wrong" means -- even when the two
/// are equal. Metadata counts only when both axes are finite and positive: one
/// good axis is not a calibration, since a perimeter does not scale by one
/// factor. An invalid override (a hand-edited store) is treated as absent.
[[nodiscard]] Calibration effectiveCalibration(double metadataXMetres, double metadataYMetres,
                                               std::optional<PixelSize> overrideMicrons);

/// "metadata", "manual" or "none" -- the spelling both exports write.
[[nodiscard]] const char* calibrationSourceName(CalibrationSource source);

/// One user decision: this scene is to be measured at this pixel size.
///
/// Keyed by (slideId, sceneIndex), as the annotation file is: scenes of one
/// file can have different pixel sizes, and an override set for one must not
/// apply to another. `slideDisplayName` is for display only and never matched.
struct CalibrationOverride
{
    std::string slideId;
    int sceneIndex = 0;
    PixelSize micronsPerPixel{0.0, 0.0};
    std::string slideDisplayName;
};

/// Persistence of calibration overrides. A preference, not annotation data:
/// it is not on the undo stack and it is not written to the annotation file,
/// which an older build would rewrite without it.
class ICalibrationOverrideStore
{
public:
    virtual ~ICalibrationOverrideStore() = default;
    /// Empty for an empty slide id, and for a stored value that is not a valid
    /// override.
    [[nodiscard]] virtual std::optional<CalibrationOverride> find(const std::string& slideId,
                                                                  int sceneIndex) const = 0;
    /// Refuses -- returns false and stores nothing -- an empty slide id, which
    /// would key one override to every unidentified slide, and an invalid value.
    virtual bool set(const CalibrationOverride& entry) = 0;
    virtual void remove(const std::string& slideId, int sceneIndex) = 0;
    [[nodiscard]] virtual std::vector<CalibrationOverride> all() const = 0;
};

} // namespace slideio::viewer::core
