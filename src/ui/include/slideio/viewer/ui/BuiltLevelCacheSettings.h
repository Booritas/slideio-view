#pragma once

#include <QString>
#include <QStringList>

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

/// Whether `edited` differs from `current` in anything stored: enabled, the
/// folder as stored, or the clamped retention. Preferences writes the shared
/// store only when it does -- every instance shares the store and each holds
/// its own copy, so writing an untouched copy back on an OK that changed
/// something else would revert another instance's newer choice.
bool builtLevelCacheSettingsChanged(const BuiltLevelCacheSettings& current, const BuiltLevelCacheSettings& edited);

/// The folders a sweep should visit after `next` takes effect: its resolved
/// folder whatever `enabled` says -- turning the cache off does not delete
/// entries, their retention does -- and, when `previous` is given and resolves
/// to another folder, that one too, so entries left behind by a folder change
/// still expire. Paths are cleaned; on Windows the comparison ignores case.
QStringList builtLevelCacheFoldersToSweep(const BuiltLevelCacheSettings& next,
                                          const BuiltLevelCacheSettings* previous);

} // namespace slideio::viewer::ui
