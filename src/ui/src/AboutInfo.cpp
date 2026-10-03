#include "slideio/viewer/ui/AboutInfo.h"

#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/BuildInfo.h"

#include "slideio/viewer/infra/SlideIOAdapter.h"

#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>

namespace slideio::viewer::ui
{

namespace
{

// Widest label below, plus the colon and a space, so the values line up in the
// fixed-width font the dialog uses.
constexpr int kLabelWidth = 20;

QString row(const QString& label, const QString& value)
{
    return QStringLiteral("%1 %2").arg((label + QLatin1Char(':')).leftJustified(kLabelWidth), value);
}

QString orUnknown(const QString& value)
{
    return value.isEmpty() ? QStringLiteral("unknown") : value;
}

// Qt's build and runtime versions differ when the application is deployed
// against a different Qt than it was compiled with, which is exactly the kind
// of mismatch a bug report needs to show. When they agree, saying it twice is
// noise.
QString qtVersionText(const AboutInfo& info)
{
    if (info.qtBuildVersion == info.qtRuntimeVersion) {
        return orUnknown(info.qtRuntimeVersion);
    }
    return QStringLiteral("%1 (built against %2)")
        .arg(orUnknown(info.qtRuntimeVersion), orUnknown(info.qtBuildVersion));
}

} // namespace

AboutInfo collectAboutInfo(const GpuInfo& gpu)
{
    AboutInfo info;

    info.appVersion = QString::fromLatin1(kAppVersion);
    info.gitRevision = QString::fromLatin1(kGitRevision);
    info.buildDate = QString::fromLatin1(kBuildDate);
    info.buildType = QString::fromLatin1(kBuildType);
    info.compiler = QStringLiteral("%1 %2").arg(QString::fromLatin1(kCompilerId),
                                                QString::fromLatin1(kCompilerVersion));
    info.jsonVersion = QString::fromLatin1(kJsonVersion);
    info.spdlogVersion = QString::fromLatin1(kSpdlogVersion);

    info.slideioVersion = QString::fromStdString(infra::slideioLibraryVersion());

    info.qtBuildVersion = QString::fromLatin1(QT_VERSION_STR);
    info.qtRuntimeVersion = QString::fromLatin1(qVersion());

    info.osName = QSysInfo::prettyProductName();
    info.kernel = QStringLiteral("%1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
    info.cpuArchitecture = QSysInfo::currentCpuArchitecture();

    info.gpu = gpu;
    info.logFile = logFilePath();

    return info;
}

QList<ThirdPartyComponent> thirdPartyComponents(const AboutInfo& info)
{
    return {
        // Qt is redistributed as shared libraries, so the LGPL notice is the
        // reason this list exists at all.
        {QStringLiteral("Qt"), orUnknown(info.qtRuntimeVersion), QStringLiteral("LGPL v3"),
         QStringLiteral("https://www.qt.io/licensing/")},
        {QStringLiteral("SlideIO"), orUnknown(info.slideioVersion), QStringLiteral("BSD 3-Clause"),
         QStringLiteral("https://github.com/Booritas/slideio")},
        {QStringLiteral("spdlog"), orUnknown(info.spdlogVersion), QStringLiteral("MIT"),
         QStringLiteral("https://github.com/gabime/spdlog")},
        {QStringLiteral("nlohmann/json"), orUnknown(info.jsonVersion), QStringLiteral("MIT"),
         QStringLiteral("https://github.com/nlohmann/json")},
    };
}

QString formatVersionInfo(const AboutInfo& info)
{
    QStringList lines;
    lines << row(QStringLiteral("SlideIO Viewer"),
                 QStringLiteral("%1 (%2)").arg(orUnknown(info.appVersion), orUnknown(info.gitRevision)));
    lines << row(QStringLiteral("Build date"), orUnknown(info.buildDate));
    lines << row(QStringLiteral("Build type"), orUnknown(info.buildType));
    lines << row(QStringLiteral("Compiler"), orUnknown(info.compiler));
    lines << row(QStringLiteral("SlideIO library"), orUnknown(info.slideioVersion));
    return lines.join(QLatin1Char('\n'));
}

QString formatSystemInfo(const AboutInfo& info)
{
    QStringList lines;
    lines << row(QStringLiteral("Operating system"), orUnknown(info.osName));
    lines << row(QStringLiteral("Kernel"), orUnknown(info.kernel));
    lines << row(QStringLiteral("CPU architecture"), orUnknown(info.cpuArchitecture));
    lines << row(QStringLiteral("Qt"), qtVersionText(info));

    if (info.gpu.isValid()) {
        lines << row(QStringLiteral("GPU vendor"), orUnknown(info.gpu.vendor));
        lines << row(QStringLiteral("GPU renderer"), info.gpu.renderer);
        lines << row(QStringLiteral("OpenGL version"), orUnknown(info.gpu.version));
        lines << row(QStringLiteral("GLSL version"), orUnknown(info.gpu.shadingLanguageVersion));
    } else {
        // Blank rows would read like a bug in this dialog rather than what it
        // actually is: the viewport never got an OpenGL 3.3 core context, which
        // is the first thing worth knowing about a rendering problem.
        lines << row(QStringLiteral("OpenGL"), QStringLiteral("not available (no rendering context)"));
    }

    lines << row(QStringLiteral("Log file"), orUnknown(info.logFile));
    return lines.join(QLatin1Char('\n'));
}

QString formatThirdPartyNotices(const QList<ThirdPartyComponent>& components)
{
    QStringList lines;
    for (const ThirdPartyComponent& component : components) {
        lines << QStringLiteral("%1 %2 — %3\n    %4")
                     .arg(component.name, component.version, component.license, component.url);
    }
    lines << QStringLiteral(
        "\nSlideIO statically links further third-party libraries (OpenCV, DCMTK,\n"
        "libtiff, GDAL and others), each under its own licence. See the SlideIO\n"
        "project for the full list.");
    return lines.join(QLatin1Char('\n'));
}

QString formatAboutReport(const AboutInfo& info)
{
    // The home page goes under the heading rather than into the version section:
    // the dialog shows it as a link, which a pasted bug report cannot carry, and
    // the version tab stays diagnostics only.
    return QStringLiteral("SlideIO Viewer\n==============\n%1\n\n%2\n\nSystem\n------\n\n%3\n\n"
                          "Third-party components\n----------------------\n\n%4\n")
        .arg(QString::fromLatin1(kProjectHomePage), formatVersionInfo(info), formatSystemInfo(info),
             formatThirdPartyNotices(thirdPartyComponents(info)));
}

QString licenseText()
{
    return QString::fromUtf8(kLicenseText).trimmed();
}

} // namespace slideio::viewer::ui
