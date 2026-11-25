#pragma once

#include "slideio/viewer/core/ResultsAttachment.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

class QSettings;

namespace slideio::viewer::infra
{

/// Per-scene results attachment settings under
/// "results/attachments/<slideId>.s<scene>", and class colour overrides under
/// "results/classColours/<caseFoldKey>".
class QSettingsResultsSettingsStore : public core::IResultsSettingsStore
{
public:
    QSettingsResultsSettingsStore();
    explicit QSettingsResultsSettingsStore(const std::string& iniFilePath);
    ~QSettingsResultsSettingsStore() override;

    [[nodiscard]] std::optional<core::ResultsAttachmentSettings> find(const std::string& slideId,
                                                                      int scene) const override;
    bool set(const core::ResultsAttachmentSettings& entry) override;
    void remove(const std::string& slideId, int scene) override;
    [[nodiscard]] std::map<std::string, core::Color> classColours() const override;
    void setClassColour(const std::string& className, core::Color colour) override;

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace slideio::viewer::infra
