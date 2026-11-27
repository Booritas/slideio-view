#include "slideio/viewer/ui/BuiltLevelCacheSettings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace slideio::viewer::ui
{

namespace
{
const QString kEnabledKey = QStringLiteral("levelCache/enabled");
const QString kFolderKey = QStringLiteral("levelCache/folder");
const QString kRetentionKey = QStringLiteral("levelCache/retentionDays");
constexpr int kMinRetentionDays = 1;
constexpr int kMaxRetentionDays = 365;
} // namespace

QString defaultBuiltLevelCacheFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/level-cache");
}

QString resolveBuiltLevelCacheFolder(const QString& stored)
{
    const QString trimmed = stored.trimmed();
    return trimmed.isEmpty() ? defaultBuiltLevelCacheFolder() : trimmed;
}

int clampCacheRetentionDays(int days)
{
    return std::clamp(days, kMinRetentionDays, kMaxRetentionDays);
}

BuiltLevelCacheSettings loadBuiltLevelCacheSettings(QSettings& settings)
{
    BuiltLevelCacheSettings value;
    value.enabled = settings.value(kEnabledKey, false).toBool();
    value.folder = settings.value(kFolderKey, QString()).toString();
    value.retentionDays =
        clampCacheRetentionDays(settings.value(kRetentionKey, kDefaultCacheRetentionDays).toInt());
    return value;
}

void saveBuiltLevelCacheSettings(QSettings& settings, const BuiltLevelCacheSettings& value)
{
    settings.setValue(kEnabledKey, value.enabled);
    settings.setValue(kFolderKey, value.folder);
    settings.setValue(kRetentionKey, clampCacheRetentionDays(value.retentionDays));
}

bool builtLevelCacheSettingsChanged(const BuiltLevelCacheSettings& current, const BuiltLevelCacheSettings& edited)
{
    // The folder as stored, exactly: a blank entry and the default folder typed
    // out are different stored values, and the user may mean either.
    return current.enabled != edited.enabled || current.folder != edited.folder
           || clampCacheRetentionDays(current.retentionDays) != clampCacheRetentionDays(edited.retentionDays);
}

QStringList builtLevelCacheFoldersToSweep(const BuiltLevelCacheSettings& next, const BuiltLevelCacheSettings* previous)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif
    QStringList folders{QDir::cleanPath(resolveBuiltLevelCacheFolder(next.folder))};
    if (previous != nullptr) {
        const QString before = QDir::cleanPath(resolveBuiltLevelCacheFolder(previous->folder));
        if (before.compare(folders.front(), kPathCase) != 0) {
            folders.append(before);
        }
    }
    return folders;
}

} // namespace slideio::viewer::ui
