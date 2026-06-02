#include "slideio/viewer/ui/LogSettings.h"

#include <spdlog/spdlog.h>

#include <QSettings>
#include <QString>

namespace
{
const QString kAppLevelKey = "logging/appLevel";
const QString kPerfEnabledKey = "logging/perfEnabled";
} // anonymous namespace

namespace slideio::viewer::ui
{

spdlog::level::level_enum levelFromString(const std::string& text)
{
    if (text == "trace") return spdlog::level::trace;
    if (text == "debug") return spdlog::level::debug;
    if (text == "info") return spdlog::level::info;
    if (text == "warn" || text == "warning") return spdlog::level::warn;
    if (text == "err" || text == "error") return spdlog::level::err;
    if (text == "off") return spdlog::level::off;
    return spdlog::level::debug; // default
}

std::string levelToString(spdlog::level::level_enum level)
{
    switch (level) {
    case spdlog::level::trace: return "trace";
    case spdlog::level::debug: return "debug";
    case spdlog::level::info: return "info";
    case spdlog::level::warn: return "warn";
    case spdlog::level::err: return "error";
    case spdlog::level::off: return "off";
    default: return "debug";
    }
}

bool resolveStartupPerfEnabled(const std::optional<std::string>& envValue, bool persisted)
{
    if (envValue.has_value() && !envValue->empty()) {
        return *envValue != "0";
    }
    return persisted;
}

std::string readAppLevelSetting()
{
    QSettings settings;
    return settings.value(kAppLevelKey).toString().toStdString();
}

bool readPerfEnabledSetting()
{
    QSettings settings;
    return settings.value(kPerfEnabledKey, false).toBool();
}

void saveLogSettings(spdlog::level::level_enum appLevel, bool perfEnabled)
{
    QSettings settings;
    settings.setValue(kAppLevelKey, QString::fromStdString(levelToString(appLevel)));
    settings.setValue(kPerfEnabledKey, perfEnabled);
}

void applyAppLevel(spdlog::level::level_enum level)
{
    if (auto viewer = spdlog::get("viewer")) {
        viewer->set_level(level);
    }
}

void applyPerfEnabled(bool enabled)
{
    if (auto perf = spdlog::get("perf")) {
        perf->set_level(enabled ? spdlog::level::trace : spdlog::level::off);
        if (enabled) {
            perf->flush_on(spdlog::level::trace);
        }
    }
}

} // namespace slideio::viewer::ui
