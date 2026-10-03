#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/DriverFilters.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace slideio::viewer::ui;

TEST_CASE("driverIdForPath selects the DICOM driver for a directory", "[ui][DriverFilters]")
{
    // SlideIO's DCM driver is the only one that accepts a directory: it walks
    // the tree and groups the files it finds by Series UID.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    REQUIRE(driverIdForPath(dir.path()) == QStringLiteral("DCM"));
}

TEST_CASE("driverIdForPath leaves a regular file on auto-detect", "[ui][DriverFilters]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString filePath = QDir(dir.path()).filePath(QStringLiteral("slide.svs"));
    QFile file(filePath);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.close();

    REQUIRE(driverIdForPath(filePath).isEmpty());
}

TEST_CASE("driverIdForPath leaves a nonexistent path on auto-detect", "[ui][DriverFilters]")
{
    // A path that is not there yet is not a directory, so there is nothing to
    // say about it -- let SlideIO produce the "cannot find driver" error.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString missing = QDir(dir.path()).filePath(QStringLiteral("not-here"));

    REQUIRE(driverIdForPath(missing).isEmpty());
}

// --- Characterisation coverage for the pre-existing filter helpers. These
// --- round-trip through the exact strings Qt hands back from the Open dialog,
// --- which is the part most likely to break silently.

TEST_CASE("buildOpenFilterString joins entries the way QFileDialog expects", "[ui][DriverFilters]")
{
    QList<DriverFilter> filters;
    filters << DriverFilter{QStringLiteral("Aperio SVS"), QStringLiteral("SVS"),
                            QStringList() << QStringLiteral("svs")};
    filters << DriverFilter{QStringLiteral("OME-TIFF"), QStringLiteral("OMETIFF"),
                            QStringList() << QStringLiteral("ome.tif") << QStringLiteral("ome.tiff")};
    filters << DriverFilter{QStringLiteral("All Files"), QString(),
                            QStringList() << QStringLiteral("*")};

    REQUIRE(buildOpenFilterString(filters)
            == QStringLiteral("Aperio SVS (*.svs);;OME-TIFF (*.ome.tif *.ome.tiff);;All Files (*)"));
}

TEST_CASE("driverIdForFilter round-trips every entry it was built from", "[ui][DriverFilters]")
{
    const QList<DriverFilter> filters = availableDriverFilters();
    REQUIRE_FALSE(filters.isEmpty());

    // buildOpenFilterString emits one line per entry, in order, so line i must
    // map back to entry i's driver id.
    const QStringList lines = buildOpenFilterString(filters).split(QStringLiteral(";;"));
    REQUIRE(lines.size() == filters.size());

    for (int i = 0; i < filters.size(); ++i) {
        INFO("filter line: " << lines[i].toStdString());
        REQUIRE(driverIdForFilter(lines[i], filters) == filters[i].driverId);
    }
}

TEST_CASE("driverIdForFilter returns no driver for the catch-all entries", "[ui][DriverFilters]")
{
    const QList<DriverFilter> filters = availableDriverFilters();
    REQUIRE(driverIdForFilter(QStringLiteral("All Files (*)"), filters).isEmpty());
}

TEST_CASE("driverIdForFilter returns no driver for an unknown filter line", "[ui][DriverFilters]")
{
    const QList<DriverFilter> filters = availableDriverFilters();
    REQUIRE(driverIdForFilter(QStringLiteral("Nonsense (*.zzz)"), filters).isEmpty());
}

TEST_CASE("isDicomDriverAvailable agrees with the driver list", "[ui][DriverFilters]")
{
    bool inFilterList = false;
    for (const auto& f : availableDriverFilters()) {
        if (f.driverId == QStringLiteral("DCM")) {
            inFilterList = true;
            break;
        }
    }
    REQUIRE(isDicomDriverAvailable() == inFilterList);
}

TEST_CASE("isDicomDriverAvailable is true for the SlideIO build we link", "[ui][DriverFilters]")
{
    // The folder-open action and folder drag/drop are gated on this. If the
    // SlideIO build ever drops the DCM driver, this is the test that says so.
    REQUIRE(isDicomDriverAvailable());
}
