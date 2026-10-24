#pragma once

#include "slideio/viewer/core/Calibration.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class QSettings;

namespace slideio::viewer::infra
{

/// Per-scene calibration overrides, persisted in QSettings under
/// "calibration/overrides/<slideId>.s<sceneIndex>".
///
/// Nothing is pruned, as with the ICC overrides: each entry is a deliberate
/// choice about one scene, and evicting one silently would put a wrong
/// measurement back without telling anybody. Removal is explicit.
class QSettingsCalibrationOverrideStore : public core::ICalibrationOverrideStore
{
public:
    /// The application's own settings, as QSettings resolves them from the
    /// organisation and application names.
    QSettingsCalibrationOverrideStore();

    /// An explicit INI file. Tests use this so they never touch the user's
    /// real configuration.
    explicit QSettingsCalibrationOverrideStore(const std::string& iniFilePath);

    ~QSettingsCalibrationOverrideStore() override;

    [[nodiscard]] std::optional<core::CalibrationOverride> find(const std::string& slideId,
                                                                int sceneIndex) const override;
    bool set(const core::CalibrationOverride& entry) override;
    void remove(const std::string& slideId, int sceneIndex) override;
    [[nodiscard]] std::vector<core::CalibrationOverride> all() const override;

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace slideio::viewer::infra
