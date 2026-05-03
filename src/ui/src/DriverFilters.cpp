#include "slideio/viewer/ui/DriverFilters.h"

#include "slideio/viewer/infra/SlideIOAdapter.h"

#include <QSet>

namespace slideio::viewer::ui
{

namespace
{

// Static label/extension table for the SlideIO drivers we know about. Drivers
// SlideIO reports that aren't in this table are silently skipped from the
// dropdown (they remain reachable via the "All Files" filter + auto-detect).
struct KnownDriver
{
    const char* driverId;
    const char* displayName;
    const char* extensions;  // space-separated, no dots
};

constexpr KnownDriver kKnownDrivers[] = {
    {"AFI",     "Aperio FluoroImage",  "afi"},
    {"CZI",     "Carl Zeiss CZI",      "czi"},
    {"DCM",     "DICOM",               "dcm"},
    {"GDAL",    "Generic (GDAL)",      "tif tiff png jpg jpeg gif bmp"},
    {"NDPI",    "Hamamatsu NDPI",      "ndpi"},
    {"OMETIFF", "OME-TIFF",            "ome.tif ome.tiff"},
    {"QPTIFF",  "PerkinElmer QPTIFF",  "qptiff"},
    {"SCN",     "Leica SCN",           "scn"},
    {"SVS",     "Aperio SVS",          "svs"},
    {"VSI",     "Olympus VSI",         "vsi"},
    {"ZVI",     "Carl Zeiss ZVI",      "zvi"},
};

QStringList splitExtensions(const char* spaceSeparated)
{
    QStringList out;
    for (const QString& e : QString::fromLatin1(spaceSeparated).split(' ', Qt::SkipEmptyParts)) {
        out << e.toLower();
    }
    return out;
}

QString joinExtensionPatterns(const QStringList& exts)
{
    QStringList patterns;
    patterns.reserve(exts.size());
    for (const QString& e : exts) {
        patterns << QStringLiteral("*.") + e;
    }
    return patterns.join(QLatin1Char(' '));
}

} // namespace

QList<DriverFilter> availableDriverFilters()
{
    // Build per-driver entries by intersecting the static table with the
    // drivers SlideIO actually exposes at runtime.
    auto available = slideio::viewer::infra::SlideIOAdapter::availableDriverIds();
    QSet<QString> availableSet;
    for (const auto& id : available) {
        availableSet.insert(QString::fromStdString(id));
    }

    QList<DriverFilter> driverEntries;
    QStringList allExtensions;
    for (const auto& known : kKnownDrivers) {
        QString id = QString::fromLatin1(known.driverId);
        if (!availableSet.contains(id)) continue;
        DriverFilter f;
        f.displayName = QString::fromLatin1(known.displayName);
        f.driverId = id;
        f.extensions = splitExtensions(known.extensions);
        driverEntries << f;
        for (const QString& e : f.extensions) {
            if (!allExtensions.contains(e)) allExtensions << e;
        }
    }

    QList<DriverFilter> result;

    // Lead with "All Supported" if we found any known drivers.
    if (!allExtensions.isEmpty()) {
        DriverFilter all;
        all.displayName = QStringLiteral("All Supported Slides");
        all.driverId.clear();
        all.extensions = allExtensions;
        result << all;
    }

    result.append(driverEntries);

    // Always offer All Files at the end so the user can pick anything.
    DriverFilter anyFile;
    anyFile.displayName = QStringLiteral("All Files");
    anyFile.driverId.clear();
    anyFile.extensions = QStringList() << QStringLiteral("*");
    result << anyFile;

    return result;
}

QString buildOpenFilterString(const QList<DriverFilter>& filters)
{
    QStringList parts;
    parts.reserve(filters.size());
    for (const auto& f : filters) {
        QString patterns = (f.extensions.size() == 1 && f.extensions.first() == QLatin1String("*"))
            ? QStringLiteral("*")
            : joinExtensionPatterns(f.extensions);
        parts << QStringLiteral("%1 (%2)").arg(f.displayName, patterns);
    }
    return parts.join(QStringLiteral(";;"));
}

QString driverIdForFilter(const QString& selectedFilter, const QList<DriverFilter>& filters)
{
    // Match by reconstructing each entry's filter line and comparing to what
    // Qt handed us back. Avoids parsing brittleness around extra whitespace.
    for (const auto& f : filters) {
        QString patterns = (f.extensions.size() == 1 && f.extensions.first() == QLatin1String("*"))
            ? QStringLiteral("*")
            : joinExtensionPatterns(f.extensions);
        QString line = QStringLiteral("%1 (%2)").arg(f.displayName, patterns);
        if (line == selectedFilter) return f.driverId;
    }
    return QString();
}

} // namespace slideio::viewer::ui
