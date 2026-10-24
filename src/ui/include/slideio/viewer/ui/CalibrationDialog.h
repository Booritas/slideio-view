#pragma once

#include "slideio/viewer/core/Calibration.h"

#include <QDialog>

#include <memory>
#include <optional>

namespace slideio::viewer::ui
{

/// View > Calibration...: the open scene's pixel size from its metadata, and a
/// manual value that overrides it. Thin by design -- what it prefills and what
/// it refuses are CalibrationPolicy's, where a test can reach them.
class CalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Outcome
    {
        Cancelled,
        Applied,
        Reset,
    };

    /// `metadataXMetres`/`metadataYMetres` are SlideInfo::resolutionX/Y.
    CalibrationDialog(double metadataXMetres, double metadataYMetres,
                      std::optional<core::PixelSize> currentOverride, QWidget* parent = nullptr);
    ~CalibrationDialog() override;

    [[nodiscard]] Outcome outcome() const;
    /// The value to store, microns per pixel. Meaningful when outcome() is
    /// Applied, and then always a valid override.
    [[nodiscard]] core::PixelSize value() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
