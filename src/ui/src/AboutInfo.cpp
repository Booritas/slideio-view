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

// A component whose version is not tracked shows its name alone, rather than a
// trailing "unknown" that would suggest the build failed to find it.
QString nameWithVersion(const ThirdPartyComponent& component)
{
    if (component.version.isEmpty()) {
        return component.name;
    }
    return QStringLiteral("%1 %2").arg(component.name, component.version);
}

// The source archive of the exact Qt release in use, which is what the LGPL
// asks to be offered. Falls back to the archive root when the version is not
// a plain major.minor.patch.
QString qtSourceUrl(const QString& version)
{
    const QStringList parts = version.split(QLatin1Char('.'));
    if (parts.size() != 3) {
        return QStringLiteral("https://download.qt.io/archive/qt/");
    }
    return QStringLiteral("https://download.qt.io/archive/qt/%1.%2/%3/single/")
        .arg(parts[0], parts[1], version);
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
         QStringLiteral("https://www.qt.io/licensing/"), false, qtSourceUrl(info.qtRuntimeVersion)},
        {QStringLiteral("SlideIO"), orUnknown(info.slideioVersion), QStringLiteral("BSD 3-Clause"),
         QStringLiteral("https://github.com/Booritas/slideio")},
        {QStringLiteral("spdlog"), orUnknown(info.spdlogVersion), QStringLiteral("MIT"),
         QStringLiteral("https://github.com/gabime/spdlog")},
        {QStringLiteral("nlohmann/json"), orUnknown(info.jsonVersion), QStringLiteral("MIT"),
         QStringLiteral("https://github.com/nlohmann/json")},

        // Linked into SlideIO. The list follows the find_package calls of the
        // extern/slideio build; a library added there belongs here too.
        {QStringLiteral("OpenCV"), QString(), QStringLiteral("Apache 2.0"),
         QStringLiteral("https://opencv.org/license/"), true},
        {QStringLiteral("DCMTK"), QString(), QStringLiteral("BSD 3-Clause"),
         QStringLiteral("https://dicom.offis.de/dcmtk"), true},
        {QStringLiteral("LibTIFF"), QString(), QStringLiteral("LibTIFF licence (BSD-style)"),
         QStringLiteral("https://libtiff.gitlab.io/libtiff/"), true},
        {QStringLiteral("libjpeg"), QString(), QStringLiteral("IJG licence"),
         QStringLiteral("https://ijg.org/"), true},
        {QStringLiteral("libjpeg-turbo"), QString(), QStringLiteral("IJG, BSD 3-Clause and zlib"),
         QStringLiteral("https://libjpeg-turbo.org/"), true},
        {QStringLiteral("OpenJPEG"), QString(), QStringLiteral("BSD 2-Clause"),
         QStringLiteral("https://www.openjpeg.org/"), true},
        {QStringLiteral("jxrlib"), QString(), QStringLiteral("BSD 2-Clause"),
         QStringLiteral("https://github.com/4creators/jxrlib"), true},
        {QStringLiteral("FreeImage"), QString(), QStringLiteral("FreeImage Public License"),
         QStringLiteral("https://freeimage.sourceforge.io/license.html"), true},
        {QStringLiteral("libpng"), QString(), QStringLiteral("libpng licence"),
         QStringLiteral("https://www.libpng.org/pub/png/libpng.html"), true},
        {QStringLiteral("libwebp"), QString(), QStringLiteral("BSD 3-Clause"),
         QStringLiteral("https://chromium.googlesource.com/webm/libwebp"), true},
        {QStringLiteral("Little CMS"), QString(), QStringLiteral("MIT"),
         QStringLiteral("https://www.littlecms.com/"), true},
        {QStringLiteral("zlib"), QString(), QStringLiteral("zlib licence"),
         QStringLiteral("https://zlib.net/zlib_license.html"), true},
        {QStringLiteral("Zstandard"), QString(), QStringLiteral("BSD 3-Clause"),
         QStringLiteral("https://github.com/facebook/zstd"), true},
        {QStringLiteral("libdeflate"), QString(), QStringLiteral("MIT"),
         QStringLiteral("https://github.com/ebiggers/libdeflate"), true},
        {QStringLiteral("XZ Utils (liblzma)"), QString(), QStringLiteral("0BSD / public domain"),
         QStringLiteral("https://tukaani.org/xz/"), true},
        {QStringLiteral("JBIG-KIT"), QString(), QStringLiteral("GPL v2 or later"),
         QStringLiteral("https://www.cl.cam.ac.uk/~mgk25/jbigkit/"), true,
         QStringLiteral("https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/")},
        {QStringLiteral("LERC"), QString(), QStringLiteral("Apache 2.0"),
         QStringLiteral("https://github.com/Esri/lerc"), true},
        {QStringLiteral("SQLite"), QString(), QStringLiteral("Public domain"),
         QStringLiteral("https://www.sqlite.org/copyright.html"), true},
        {QStringLiteral("TinyXML-2"), QString(), QStringLiteral("zlib licence"),
         QStringLiteral("https://github.com/leethomason/tinyxml2"), true},
        {QStringLiteral("ICU"), QString(), QStringLiteral("Unicode License"),
         QStringLiteral("https://github.com/unicode-org/icu"), true},
        {QStringLiteral("libiconv"), QString(), QStringLiteral("LGPL v2.1"),
         QStringLiteral("https://www.gnu.org/software/libiconv/"), true,
         QStringLiteral("https://ftp.gnu.org/pub/gnu/libiconv/")},
        {QStringLiteral("POLE"), QString(), QStringLiteral("BSD 2-Clause"),
         QStringLiteral("https://github.com/catlan/pole"), true},
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
    bool slideioHeadingWritten = false;
    for (const ThirdPartyComponent& component : components) {
        if (component.viaSlideio && !slideioHeadingWritten) {
            lines << QStringLiteral("\nLinked into the SlideIO library:");
            slideioHeadingWritten = true;
        }
        lines << QStringLiteral("%1 — %2\n    %3")
                     .arg(nameWithVersion(component), component.license, component.url);
    }

    bool sourceHeadingWritten = false;
    for (const ThirdPartyComponent& component : components) {
        if (component.sourceUrl.isEmpty()) {
            continue;
        }
        if (!sourceHeadingWritten) {
            lines << QStringLiteral("\nSource code of the components under the GPL and LGPL:");
            sourceHeadingWritten = true;
        }
        lines << QStringLiteral("%1\n    %2").arg(nameWithVersion(component), component.sourceUrl);
    }
    return lines.join(QLatin1Char('\n'));
}

QString formatThirdPartyHtml(const QList<ThirdPartyComponent>& components)
{
    const auto item = [](const ThirdPartyComponent& component) {
        return QStringLiteral("<li><b>%1</b> &mdash; %2<br/>"
                              "<a style='color:#6FA8E0;' href='%3'>%3</a></li>")
            .arg(nameWithVersion(component).toHtmlEscaped(), component.license.toHtmlEscaped(),
                 component.url.toHtmlEscaped());
    };

    QString direct;
    QString viaSlideio;
    for (const ThirdPartyComponent& component : components) {
        (component.viaSlideio ? viaSlideio : direct) += item(component);
    }

    QString html = QStringLiteral("<body style='color:#CCCCCC;'>");
    html += QStringLiteral("<p>SlideIO Viewer is distributed with the following components:</p><ul>");
    html += direct;
    html += QStringLiteral("</ul>");
    if (!viaSlideio.isEmpty()) {
        html += QStringLiteral("<p>The SlideIO library links the following components:</p><ul>");
        html += viaSlideio;
        html += QStringLiteral("</ul>");
    }
    html += QStringLiteral("<p>Each component is used under its own licence, which can be read at "
                           "the address given.</p>");

    QString sources;
    for (const ThirdPartyComponent& component : components) {
        if (!component.sourceUrl.isEmpty()) {
            sources += QStringLiteral("<li>%1: <a style='color:#6FA8E0;' href='%2'>%2</a></li>")
                           .arg(nameWithVersion(component).toHtmlEscaped(), component.sourceUrl.toHtmlEscaped());
        }
    }
    if (!sources.isEmpty()) {
        html += QStringLiteral("<p>The source code of the components under the GPL and LGPL is "
                               "available from:</p><ul>");
        html += sources;
        html += QStringLiteral("</ul>");
    }
    html += QStringLiteral("</body>");
    return html;
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
