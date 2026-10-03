#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace slideio::viewer::ui
{

// One entry in the Open File dialog's filter dropdown. driverId is empty for
// the "All Supported" / "All Files" entries (auto-detect).
struct DriverFilter
{
    QString displayName;     // e.g., "Aperio SVS"
    QString driverId;        // SlideIO driver id, e.g., "SVS"; empty for auto
    QStringList extensions;  // lowercase, no leading dot, e.g., {"svs"}
};

// Returns the filter list to put in the Open File dialog. The list is the
// intersection of the drivers SlideIO reports via getDriverIDs() with a static
// table of known label/extension mappings, plus an "All Supported" entry at
// the front and an "All Files" entry at the end. Drivers SlideIO reports that
// we don't have label/extension mappings for are skipped.
QList<DriverFilter> availableDriverFilters();

// Builds the Qt filter string from a list of DriverFilter entries, using the
// standard "Name (*.ext1 *.ext2);;Name2 (*.extN)" format that QFileDialog expects.
QString buildOpenFilterString(const QList<DriverFilter>& filters);

// Reverse lookup: given the filter string Qt returns from getOpenFileName's
// selectedFilter parameter, find the matching DriverFilter and return its
// driverId. Returns empty string if not found or if the matched filter is the
// "All Supported" / "All Files" entry.
QString driverIdForFilter(const QString& selectedFilter,
                          const QList<DriverFilter>& filters);

// Given local filesystem paths in drop order, returns the first one the viewer
// can open, or an empty string if none qualify. A path qualifies when it is a
// directory (readable by the DICOM driver) or a file whose extension appears in
// availableDriverFilters(), i.e. the same formats the Open dialog lists. Used to
// decide whether a drag is accepted and what gets opened on drop.
QString firstOpenableSlidePath(const QStringList& localPaths);

// True when the SlideIO build in use exposes the DICOM driver. Opening a
// directory depends on it, so the folder-open action and folder drag/drop are
// gated on this rather than offering something that cannot work.
bool isDicomDriverAvailable();

// Returns the SlideIO driver id that a given path requires, or an empty string
// to leave the choice to SlideIO's auto-detection. Only directories need a
// forced driver: DCM is the one driver that accepts a directory, and no
// extension-based filter can describe a folder.
QString driverIdForPath(const QString& path);

} // namespace slideio::viewer::ui
