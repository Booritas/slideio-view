#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/BuiltLevelCacheSettings.h"

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

using namespace slideio::viewer::ui;

TEST_CASE("an empty store loads the defaults", "[ui][BuiltLevelCacheSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);

    const BuiltLevelCacheSettings loaded = loadBuiltLevelCacheSettings(settings);
    REQUIRE_FALSE(loaded.enabled);
    REQUIRE(loaded.folder == QString());
    REQUIRE(loaded.retentionDays == 30);
    REQUIRE(kDefaultCacheRetentionDays == 30);
}

TEST_CASE("retention is clamped to one through 365 days", "[ui][BuiltLevelCacheSettings]")
{
    REQUIRE(clampCacheRetentionDays(0) == 1);
    REQUIRE(clampCacheRetentionDays(1000) == 365);
    REQUIRE(clampCacheRetentionDays(-5) == 1);
    REQUIRE(clampCacheRetentionDays(45) == 45);
    REQUIRE(clampCacheRetentionDays(1) == 1);
    REQUIRE(clampCacheRetentionDays(365) == 365);
}

TEST_CASE("a stored retention of zero loads as one day", "[ui][BuiltLevelCacheSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("levelCache/retentionDays"), 0);

    REQUIRE(loadBuiltLevelCacheSettings(settings).retentionDays == 1);
}

TEST_CASE("a blank folder resolves to the default level-cache folder", "[ui][BuiltLevelCacheSettings]")
{
    REQUIRE(defaultBuiltLevelCacheFolder().endsWith(QStringLiteral("/level-cache")));
    REQUIRE(resolveBuiltLevelCacheFolder(QStringLiteral("   ")) == defaultBuiltLevelCacheFolder());
    REQUIRE(resolveBuiltLevelCacheFolder(QString()) == defaultBuiltLevelCacheFolder());
    REQUIRE(resolveBuiltLevelCacheFolder(QStringLiteral("D:/cache")) == QStringLiteral("D:/cache"));
}

TEST_CASE("save then load round-trips under the levelCache keys", "[ui][BuiltLevelCacheSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("settings.ini");
    const QString folder = QStringLiteral("D:/x/") + QChar(0x00E7);

    {
        QSettings settings(ini, QSettings::IniFormat);
        saveBuiltLevelCacheSettings(settings, BuiltLevelCacheSettings{true, folder, 7});
    }

    QSettings settings(ini, QSettings::IniFormat);
    REQUIRE(settings.value(QStringLiteral("levelCache/enabled")).toBool());
    REQUIRE(settings.value(QStringLiteral("levelCache/folder")).toString() == folder);
    REQUIRE(settings.value(QStringLiteral("levelCache/retentionDays")).toInt() == 7);

    const BuiltLevelCacheSettings loaded = loadBuiltLevelCacheSettings(settings);
    REQUIRE(loaded.enabled);
    REQUIRE(loaded.folder == folder);
    REQUIRE(loaded.retentionDays == 7);
}

TEST_CASE("the configured folder is swept whether or not the cache is on", "[ui][BuiltLevelCacheSettings]")
{
    const BuiltLevelCacheSettings off{false, QStringLiteral("D:/cache"), 30};
    REQUIRE(builtLevelCacheFoldersToSweep(off, nullptr) == QStringList{QStringLiteral("D:/cache")});

    const BuiltLevelCacheSettings offDefault{false, QString(), 30};
    REQUIRE(builtLevelCacheFoldersToSweep(offDefault, nullptr) == QStringList{defaultBuiltLevelCacheFolder()});
}

TEST_CASE("a changed folder sweeps the previous one too", "[ui][BuiltLevelCacheSettings]")
{
    const BuiltLevelCacheSettings before{true, QStringLiteral("D:/old"), 30};
    const BuiltLevelCacheSettings after{false, QStringLiteral("D:/new"), 30};
    REQUIRE(builtLevelCacheFoldersToSweep(after, &before)
            == QStringList{QStringLiteral("D:/new"), QStringLiteral("D:/old")});
}

TEST_CASE("an unchanged resolved folder is swept once", "[ui][BuiltLevelCacheSettings]")
{
    SECTION("same text")
    {
        const BuiltLevelCacheSettings before{true, QStringLiteral("D:/cache"), 30};
        const BuiltLevelCacheSettings after{true, QStringLiteral("D:/cache"), 7};
        REQUIRE(builtLevelCacheFoldersToSweep(after, &before) == QStringList{QStringLiteral("D:/cache")});
    }
    SECTION("blank and the default spelled out")
    {
        const BuiltLevelCacheSettings before{true, QString(), 30};
        const BuiltLevelCacheSettings after{true, defaultBuiltLevelCacheFolder(), 30};
        REQUIRE(builtLevelCacheFoldersToSweep(after, &before).size() == 1);
    }
    SECTION("a trailing separator")
    {
        const BuiltLevelCacheSettings before{true, QStringLiteral("D:/cache/"), 30};
        const BuiltLevelCacheSettings after{true, QStringLiteral("D:/cache"), 30};
        REQUIRE(builtLevelCacheFoldersToSweep(after, &before) == QStringList{QStringLiteral("D:/cache")});
    }
}
