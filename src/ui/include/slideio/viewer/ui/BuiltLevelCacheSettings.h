#pragma once

#include <QString>

class QSettings;

namespace slideio::viewer::ui
{

/// How long an unused built-level cache entry is kept before the sweep removes it.
constexpr int kDefaultCacheRetentionDays = 30;

/// The built-level disk cache preferences, stored under `levelCache/` in QSettings.
/// Off by default. A change applies to the next slide opened.
struct BuiltLevelCacheSettings
{
    bool enabled = false;
    /// As stored: empty means the default folder (see resolveBuiltLevelCacheFolder).
    QString folder;
    int retentionDays = kDefaultCacheRetentionDays;
};

/// `<AppLocalDataLocation>/level-cache`.
QString defaultBuiltLevelCacheFolder();

/// The folder to use: the stored one, or the default when it is blank.
QString resolveBuiltLevelCacheFolder(const QString& stored);

/// Clamps a retention to [1, 365] days.
int clampCacheRetentionDays(int days);

/// Reads the settings; the retention is clamped, the folder returned as stored.
BuiltLevelCacheSettings loadBuiltLevelCacheSettings(QSettings& settings);

void saveBuiltLevelCacheSettings(QSettings& settings, const BuiltLevelCacheSettings& value);

} // namespace slideio::viewer::ui
