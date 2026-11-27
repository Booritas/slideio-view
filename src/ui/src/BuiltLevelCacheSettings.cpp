#include "slideio/viewer/ui/BuiltLevelCacheSettings.h"

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

} // namespace slideio::viewer::ui
