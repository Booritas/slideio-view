#pragma once

#include "slideio/viewer/ui/GpuInfo.h"

#include <QList>
#include <QString>

namespace slideio::viewer::ui
{

// One component shipped with the viewer, listed in the About dialog's
// third-party tab so the licences of the libraries we redistribute are
// attributed. Qt is the reason this exists: the LGPL expects the notice.
struct ThirdPartyComponent
{
    QString name;
    QString version;  // "unknown" when the build does not expose one
    QString license;
    QString url;
};

// Everything the About dialog shows. Gathering it into one struct keeps the
// formatting functions below pure, so the tests can drive them with synthetic
// values instead of whatever machine happens to run them.
struct AboutInfo
{
    QString appVersion;
    QString gitRevision;
    QString buildDate;
    QString buildType;
    QString compiler;  // "MSVC 19.44.35207.1"
    QString slideioVersion;
    QString qtBuildVersion;
    QString qtRuntimeVersion;
    QString jsonVersion;
    QString spdlogVersion;
    QString osName;
    QString kernel;  // "winnt 10.0.26200"
    QString cpuArchitecture;
    GpuInfo gpu;
    QString logFile;
};

// Fills every field from the running process and the generated build header.
// The GL strings cannot be queried here, so the viewport's captured copy is
// passed in; pass a default-constructed GpuInfo when there is no context.
AboutInfo collectAboutInfo(const GpuInfo& gpu);

// The components to attribute, with the versions this build actually uses.
QList<ThirdPartyComponent> thirdPartyComponents(const AboutInfo& info);

// Section bodies, one "Label: value" line each. Rendered in the dialog's tabs
// and concatenated into the clipboard report.
QString formatVersionInfo(const AboutInfo& info);
QString formatSystemInfo(const AboutInfo& info);
QString formatThirdPartyNotices(const QList<ThirdPartyComponent>& components);

// The plain-text blob the Copy button puts on the clipboard: the version,
// system and third-party sections under headings. The licence body is left
// out deliberately -- the report exists to be pasted into a bug report, and
// 24 lines of BSD boilerplate would bury the part that matters.
QString formatAboutReport(const AboutInfo& info);

// Full text of the viewer's own licence, embedded at configure time from the
// LICENSE file at the root of the repository.
QString licenseText();

} // namespace slideio::viewer::ui
