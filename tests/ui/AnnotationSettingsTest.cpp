#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationSettings.h"
#include "slideio/viewer/ui/AppPaths.h"

#include <QSettings>
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
        settings.setUserName("s.melnikov");
    }

    AnnotationSettings reopened(ini);
    REQUIRE(reopened.workspaceDirectory() == "D:/pathology/annotations");
    REQUIRE(reopened.userName() == "s.melnikov");
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

TEST_CASE("a user name is trimmed on the way in", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setUserName("  s.melnikov  ");
    REQUIRE(settings.userName() == "s.melnikov");
    REQUIRE(settings.hasUserName());
}
