#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/RecentFiles.h"

#include <QtGlobal>

using slideio::viewer::ui::normalizedSlidePath;
using slideio::viewer::ui::recentFilesWithPathPromoted;

TEST_CASE("normalizedSlidePath leaves an already-normalized path alone")
{
    REQUIRE(normalizedSlidePath("D:/slides/case1.svs") == QString("D:/slides/case1.svs"));
}

TEST_CASE("normalizedSlidePath drops a trailing separator from a DICOM folder")
{
    REQUIRE(normalizedSlidePath("D:/studies/case1/") == QString("D:/studies/case1"));
}

TEST_CASE("normalizedSlidePath returns an empty path unchanged")
{
    REQUIRE(normalizedSlidePath(QString()).isEmpty());
}

TEST_CASE("the promoted path goes to the front")
{
    const QStringList files{"D:/slides/a.svs", "D:/slides/b.svs"};
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/c.svs", 10);
    REQUIRE(result == QStringList{"D:/slides/c.svs", "D:/slides/a.svs", "D:/slides/b.svs"});
}

TEST_CASE("re-opening a listed slide moves it up rather than listing it twice")
{
    const QStringList files{"D:/slides/a.svs", "D:/slides/b.svs"};
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/b.svs", 10);
    REQUIRE(result == QStringList{"D:/slides/b.svs", "D:/slides/a.svs"});
}

TEST_CASE("a redundant segment does not make a second entry")
{
    const QStringList files{"D:/slides/a.svs"};
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/./a.svs", 10);
    REQUIRE(result == QStringList{"D:/slides/a.svs"});
}

TEST_CASE("the list is truncated to the maximum, oldest dropped first")
{
    QStringList files;
    for (int i = 0; i < 10; ++i) {
        files.append(QString("D:/slides/%1.svs").arg(i));
    }
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/new.svs", 10);
    REQUIRE(result.size() == 10);
    REQUIRE(result.first() == QString("D:/slides/new.svs"));
    REQUIRE(result.last() == QString("D:/slides/8.svs"));
}

TEST_CASE("a non-positive maximum yields an empty list")
{
    REQUIRE(recentFilesWithPathPromoted({"D:/slides/a.svs"}, "D:/slides/b.svs", 0).isEmpty());
}

TEST_CASE("an empty entry is dropped rather than carried")
{
    const QStringList files{"D:/slides/a.svs", QString(), "D:/slides/b.svs"};
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/c.svs", 10);
    REQUIRE(result == QStringList{"D:/slides/c.svs", "D:/slides/a.svs", "D:/slides/b.svs"});
}

// Separator style and case only identify the same file where the platform says
// they do. On Linux a backslash is an ordinary character in a file name and
// `a.svs` and `A.svs` are two files, so folding either there would rewrite a
// stored path into one that no longer opens.
#ifdef Q_OS_WIN

TEST_CASE("normalizedSlidePath converts the shell's backslashes")
{
    REQUIRE(normalizedSlidePath("D:\\slides\\case1.svs") == QString("D:/slides/case1.svs"));
}

TEST_CASE("the same slide in two separator styles is listed once")
{
    // The defect this function exists for: the file dialog and a drop give
    // forward slashes, the shell's command line gives backslashes, and the menu
    // shows only the last component -- so one slide appeared twice under one
    // name.
    const QStringList files{"D:\\slides\\a.svs", "D:/slides/b.svs"};
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/a.svs", 10);
    REQUIRE(result == QStringList{"D:/slides/a.svs", "D:/slides/b.svs"});
}

TEST_CASE("a list that already holds both spellings is repaired as it is rewritten")
{
    // No migration step: the stored list is normalized on the next open, which
    // is the only time it is written.
    const QStringList files{"D:\\slides\\a.svs", "D:/slides/a.svs"};
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/b.svs", 10);
    REQUIRE(result == QStringList{"D:/slides/b.svs", "D:/slides/a.svs"});
}

TEST_CASE("a drive letter in a different case is the same slide")
{
    const QStringList files{"d:/slides/a.svs"};
    const QStringList result = recentFilesWithPathPromoted(files, "D:/slides/A.svs", 10);
    REQUIRE(result == QStringList{"D:/slides/A.svs"});
}

#endif // Q_OS_WIN
