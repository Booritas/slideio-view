#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/DriverFilters.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace slideio::viewer::ui;

namespace
{

// Creates an empty file inside dir and returns its path.
QString touch(const QTemporaryDir& dir, const QString& name)
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.close();
    return path;
}

QString makeSubdir(const QTemporaryDir& dir, const QString& name)
{
    REQUIRE(QDir(dir.path()).mkdir(name));
    return QDir(dir.path()).filePath(name);
}

} // namespace

TEST_CASE("firstOpenableSlidePath accepts a slide file by extension", "[ui][SlideDrop]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString slide = touch(dir, QStringLiteral("tumour.svs"));

    REQUIRE(firstOpenableSlidePath(QStringList() << slide) == slide);
}

TEST_CASE("firstOpenableSlidePath accepts a directory", "[ui][SlideDrop]")
{
    // A dropped DICOM study folder has no extension to match on; it qualifies
    // because the DICOM driver reads directories.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString study = makeSubdir(dir, QStringLiteral("study-1234"));

    REQUIRE(firstOpenableSlidePath(QStringList() << study) == study);
}

TEST_CASE("firstOpenableSlidePath ignores an unsupported file", "[ui][SlideDrop]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString readme = touch(dir, QStringLiteral("README.md"));

    REQUIRE(firstOpenableSlidePath(QStringList() << readme).isEmpty());
}

TEST_CASE("firstOpenableSlidePath matches extensions case-insensitively", "[ui][SlideDrop]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString slide = touch(dir, QStringLiteral("TUMOUR.SVS"));

    REQUIRE(firstOpenableSlidePath(QStringList() << slide) == slide);
}

TEST_CASE("firstOpenableSlidePath matches a multi-suffix extension", "[ui][SlideDrop]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString slide = touch(dir, QStringLiteral("plate.ome.tif"));

    REQUIRE(firstOpenableSlidePath(QStringList() << slide) == slide);
}

TEST_CASE("firstOpenableSlidePath skips past entries it cannot open", "[ui][SlideDrop]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString readme = touch(dir, QStringLiteral("README.md"));
    const QString slide = touch(dir, QStringLiteral("tumour.svs"));

    REQUIRE(firstOpenableSlidePath(QStringList() << readme << slide) == slide);
}

TEST_CASE("firstOpenableSlidePath takes the first of several openable entries", "[ui][SlideDrop]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString first = touch(dir, QStringLiteral("a.svs"));
    const QString second = touch(dir, QStringLiteral("b.svs"));

    REQUIRE(firstOpenableSlidePath(QStringList() << first << second) == first);
}

TEST_CASE("firstOpenableSlidePath returns nothing for an empty list", "[ui][SlideDrop]")
{
    REQUIRE(firstOpenableSlidePath(QStringList()).isEmpty());
}
