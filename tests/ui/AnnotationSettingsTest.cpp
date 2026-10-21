#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationSettings.h"
#include "slideio/viewer/ui/AppPaths.h"

#include <QString>
#include <QTemporaryDir>

using slideio::viewer::ui::AnnotationSettings;

TEST_CASE("an unconfigured workspace resolves to the default", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    REQUIRE(settings.workspaceDirectory()
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
}

TEST_CASE("the default workspace is not the log directory", "[ui][AnnotationSettings]")
{
    // Logs are diagnostics and belong in app-local data; this is the user's
    // work and belongs somewhere they can find and back up.
    REQUIRE(slideio::viewer::ui::defaultAnnotationWorkspaceDirectory()
            != slideio::viewer::ui::logDirectory());
}

TEST_CASE("a configured workspace is returned verbatim", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setWorkspaceDirectory("D:/pathology/annotations");
    REQUIRE(settings.workspaceDirectory() == "D:/pathology/annotations");
}

TEST_CASE("the workspace survives across sessions", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("settings.ini");

    {
        AnnotationSettings settings(ini);
        settings.setWorkspaceDirectory("D:/pathology/annotations");
        settings.setUserName("jdoe");
    }

    AnnotationSettings reopened(ini);
    REQUIRE(reopened.workspaceDirectory() == "D:/pathology/annotations");
    REQUIRE(reopened.userName() == "jdoe");
}

TEST_CASE("clearing the workspace falls back to the default", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setWorkspaceDirectory("D:/pathology/annotations");
    settings.setWorkspaceDirectory("");
    REQUIRE(settings.workspaceDirectory()
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
}

TEST_CASE("a workspace of only whitespace is treated as unset", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setWorkspaceDirectory("   ");
    REQUIRE(settings.workspaceDirectory()
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
}

TEST_CASE("no user name is configured by default", "[ui][AnnotationSettings]")
{
    // FR-USER-01 gates annotation creation on this being set.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    REQUIRE(settings.userName().isEmpty());
    REQUIRE_FALSE(settings.hasUserName());
}

TEST_CASE("a user name of only whitespace does not count as configured",
          "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setUserName("   ");
    REQUIRE_FALSE(settings.hasUserName());
}

TEST_CASE("a user name can be cleared", "[ui][AnnotationSettings]")
{
    // Gates FR-USER-01: clearing the name must put the identity gate back up.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setUserName("jdoe");
    REQUIRE(settings.hasUserName());

    settings.setUserName("");
    REQUIRE(settings.userName().isEmpty());
    REQUIRE_FALSE(settings.hasUserName());
}

TEST_CASE("a user name is trimmed on the way in", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setUserName("  jdoe  ");
    REQUIRE(settings.userName() == "jdoe");
    REQUIRE(settings.hasUserName());
}

TEST_CASE("resolveWorkspaceDirectory applies the same rule as the getter",
          "[ui][AnnotationSettings]")
{
    // One rule, two callers: the stored value and a value the user has typed
    // but not committed. They must not drift.
    REQUIRE(AnnotationSettings::resolveWorkspaceDirectory("D:/pathology")
            == "D:/pathology");
    REQUIRE(AnnotationSettings::resolveWorkspaceDirectory("  D:/pathology  ")
            == "D:/pathology");
    REQUIRE(AnnotationSettings::resolveWorkspaceDirectory("")
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
    REQUIRE(AnnotationSettings::resolveWorkspaceDirectory("   ")
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
}

TEST_CASE("the getter and the static resolver agree on a stored value",
          "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setWorkspaceDirectory("D:/pathology/annotations");
    REQUIRE(settings.workspaceDirectory()
            == AnnotationSettings::resolveWorkspaceDirectory("D:/pathology/annotations"));

    settings.setWorkspaceDirectory("");
    REQUIRE(settings.workspaceDirectory() == AnnotationSettings::resolveWorkspaceDirectory(""));
}

// Two settings objects over one store stand in for two running instances:
// QSettings shares one cache per file within a process, so a write through one
// is seen by the other at once, as a synced write by another process is.
TEST_CASE("another instance's workspace change does not move this instance's",
          "[ui][AnnotationSettings]")
{
    // The repository root is per instance; the stored setting is shared. A
    // reader of the shared value showed instance A the folder instance B had
    // just chosen, and pressing OK in A's Preferences unedited would then have
    // switched A into B's folder, where B holds the lock.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("settings.ini");

    AnnotationSettings first(ini);
    first.setWorkspaceDirectory("D:/pathology/first");
    AnnotationSettings second(ini);

    second.setWorkspaceDirectory("D:/pathology/second");

    REQUIRE(first.workspaceDirectory() == "D:/pathology/first");
    REQUIRE(first.configuredWorkspaceDirectory() == "D:/pathology/first");
    REQUIRE(second.workspaceDirectory() == "D:/pathology/second");
}

TEST_CASE("an instance following the default is not moved by another's choice",
          "[ui][AnnotationSettings]")
{
    // The empty setting is its own state -- "follow the default" -- and is the
    // one a Cancel restores, so it has to survive another instance's write
    // too, not only the resolved path.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("settings.ini");

    AnnotationSettings first(ini);
    AnnotationSettings second(ini);

    second.setWorkspaceDirectory("D:/pathology/second");

    REQUIRE(first.workspaceDirectory()
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
    REQUIRE(first.configuredWorkspaceDirectory().isEmpty());
}

TEST_CASE("the last instance to choose a workspace is the one the next launch reads",
          "[ui][AnnotationSettings]")
{
    // What is NOT per instance: the store. A fresh start reads whichever
    // choice was written last, which is the behaviour before this change.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("settings.ini");

    {
        AnnotationSettings first(ini);
        AnnotationSettings second(ini);
        first.setWorkspaceDirectory("D:/pathology/first");
        second.setWorkspaceDirectory("D:/pathology/second");
    }

    AnnotationSettings nextLaunch(ini);
    REQUIRE(nextLaunch.workspaceDirectory() == "D:/pathology/second");
}
