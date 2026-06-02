#pragma once

#include <spdlog/common.h> // spdlog::level::level_enum

#include <optional>
#include <string>

namespace slideio::viewer::ui
{

// --- Pure helpers (no Qt, unit-tested) ---

// Maps a stored level string to a spdlog level. Accepts "trace", "debug",
// "info", "warn"/"warning", "err"/"error", "off". Unknown/empty -> debug
// (the application's default level).
spdlog::level::level_enum levelFromString(const std::string& text);

// Inverse of levelFromString for the six exposed levels. err -> "error",
// warn -> "warn"; round-trips with levelFromString.
std::string levelToString(spdlog::level::level_enum level);

// Resolves whether performance logging should be on at startup. The
// SLIDEIO_PERF_LOG environment variable wins when present and non-empty
// ("0" -> off, anything else -> on); otherwise the persisted setting is used.
bool resolveStartupPerfEnabled(const std::optional<std::string>& envValue, bool persisted);

// --- QSettings-backed glue (Qt; verified manually) ---

// Reads the persisted application log-level string ("" if never saved).
std::string readAppLevelSetting();

// Reads the persisted performance-logging flag (false if never saved).
bool readPerfEnabledSetting();

// Persists both log settings to QSettings.
void saveLogSettings(spdlog::level::level_enum appLevel, bool perfEnabled);

// --- Live-logger glue (spdlog; verified manually) ---

// Sets the "viewer" (default) logger level. No-op if the logger is absent.
void applyAppLevel(spdlog::level::level_enum level);

// Enables (trace + flush_on trace) or disables (off) the "perf" logger.
// No-op if the logger is absent.
void applyPerfEnabled(bool enabled);

} // namespace slideio::viewer::ui
