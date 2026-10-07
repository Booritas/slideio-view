#pragma once

#include "slideio/viewer/core/ColorProfileOverride.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class QSettings;

namespace slideio::viewer::infra
{

/// Per-slide profile overrides, persisted in QSettings under
/// "color/slideProfiles/<slideId>".
///
/// Nothing is pruned. Each entry is a deliberate choice the user made about one
/// slide and is a few hundred bytes; evicting one silently would discard that
/// choice without telling them. Removal is explicit.
class QSettingsColorProfileOverrideStore : public core::IColorProfileOverrideStore
{
public:
    /// The application's own settings, as QSettings resolves them from the
    /// organisation and application names.
    QSettingsColorProfileOverrideStore();

    /// An explicit INI file. Tests use this so they never touch the user's
    /// real configuration.
    explicit QSettingsColorProfileOverrideStore(const std::string& iniFilePath);

    ~QSettingsColorProfileOverrideStore() override;

    std::optional<core::ColorProfileOverride> find(const std::string& slideId) const override;
    void set(const core::ColorProfileOverride& entry) override;
    void remove(const std::string& slideId) override;
    std::vector<core::ColorProfileOverride> all() const override;

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace slideio::viewer::infra
