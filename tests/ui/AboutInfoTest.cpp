#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AboutInfo.h"

using namespace slideio::viewer::ui;

namespace
{

// A fully populated struct, so a test that checks one field does not also
// depend on which fields happen to be empty.
AboutInfo sampleInfo()
{
    AboutInfo info;
    info.appVersion = QStringLiteral("0.1.0");
    info.gitRevision = QStringLiteral("df0b444");
    info.buildDate = QStringLiteral("2026-10-03");
    info.buildType = QStringLiteral("Release");
    info.compiler = QStringLiteral("MSVC 19.44.35207.1");
    info.slideioVersion = QStringLiteral("2.10.0");
    info.qtBuildVersion = QStringLiteral("6.7.3");
    info.qtRuntimeVersion = QStringLiteral("6.7.3");
    info.jsonVersion = QStringLiteral("3.11.3");
    info.spdlogVersion = QStringLiteral("1.15.0");
    info.osName = QStringLiteral("Windows 11 Pro");
    info.kernel = QStringLiteral("winnt 10.0.26200");
    info.cpuArchitecture = QStringLiteral("x86_64");
    info.gpu.vendor = QStringLiteral("NVIDIA Corporation");
    info.gpu.renderer = QStringLiteral("NVIDIA GeForce RTX 4070");
    info.gpu.version = QStringLiteral("3.3.0 NVIDIA 560.94");
    info.gpu.shadingLanguageVersion = QStringLiteral("3.30 NVIDIA via Cg compiler");
    info.logFile = QStringLiteral("C:/Users/x/AppData/Local/SlideIO/SlideIO Viewer/logs/slideio-viewer.log");
    return info;
}

} // namespace

TEST_CASE("formatVersionInfo names the application and library versions", "[ui][AboutInfo]")
{
    const QString text = formatVersionInfo(sampleInfo());

    REQUIRE(text.contains(QStringLiteral("0.1.0")));
    REQUIRE(text.contains(QStringLiteral("df0b444")));
    REQUIRE(text.contains(QStringLiteral("2026-10-03")));
    REQUIRE(text.contains(QStringLiteral("Release")));
    REQUIRE(text.contains(QStringLiteral("MSVC 19.44.35207.1")));
    REQUIRE(text.contains(QStringLiteral("2.10.0")));
}

TEST_CASE("formatSystemInfo reports the machine, Qt and the log file", "[ui][AboutInfo]")
{
    const QString text = formatSystemInfo(sampleInfo());

    REQUIRE(text.contains(QStringLiteral("Windows 11 Pro")));
    REQUIRE(text.contains(QStringLiteral("winnt 10.0.26200")));
    REQUIRE(text.contains(QStringLiteral("x86_64")));
    REQUIRE(text.contains(QStringLiteral("6.7.3")));
    REQUIRE(text.contains(QStringLiteral("slideio-viewer.log")));
}

TEST_CASE("formatSystemInfo reports the GPU when a context came up", "[ui][AboutInfo]")
{
    const QString text = formatSystemInfo(sampleInfo());

    REQUIRE(text.contains(QStringLiteral("NVIDIA GeForce RTX 4070")));
    REQUIRE(text.contains(QStringLiteral("3.3.0 NVIDIA 560.94")));
    REQUIRE(text.contains(QStringLiteral("3.30 NVIDIA via Cg compiler")));
}

TEST_CASE("formatSystemInfo says so rather than printing blank GPU rows", "[ui][AboutInfo]")
{
    // Without a 3.3 core context ViewportWidget captures nothing. A row of
    // empty values reads like a bug in the dialog; this is the case a bug
    // report most needs to be explicit about.
    AboutInfo info = sampleInfo();
    info.gpu = GpuInfo{};
    REQUIRE_FALSE(info.gpu.isValid());

    const QString text = formatSystemInfo(info);

    REQUIRE(text.contains(QStringLiteral("not available")));
    REQUIRE_FALSE(text.contains(QStringLiteral("NVIDIA")));
}

TEST_CASE("thirdPartyComponents attributes every redistributed library", "[ui][AboutInfo]")
{
    const QList<ThirdPartyComponent> components = thirdPartyComponents(sampleInfo());
    const QString text = formatThirdPartyNotices(components);

    for (const QString& name : {QStringLiteral("Qt"), QStringLiteral("SlideIO"),
                                QStringLiteral("spdlog"), QStringLiteral("nlohmann/json")}) {
        INFO(name.toStdString());
        REQUIRE(text.contains(name));
    }

    // Qt is redistributed as shared libraries, so naming its licence is the
    // whole point of the tab.
    REQUIRE(text.contains(QStringLiteral("LGPL")));
}

TEST_CASE("thirdPartyComponents carries the versions this build uses", "[ui][AboutInfo]")
{
    const QList<ThirdPartyComponent> components = thirdPartyComponents(sampleInfo());

    const auto find = [&components](const QString& name) {
        for (const ThirdPartyComponent& component : components) {
            if (component.name == name) {
                return component;
            }
        }
        FAIL("no component named " + name.toStdString());
        return ThirdPartyComponent{};
    };

    REQUIRE(find(QStringLiteral("SlideIO")).version == QStringLiteral("2.10.0"));
    REQUIRE(find(QStringLiteral("Qt")).version == QStringLiteral("6.7.3"));
    REQUIRE(find(QStringLiteral("spdlog")).version == QStringLiteral("1.15.0"));
}

TEST_CASE("every component has a licence and a URL to follow", "[ui][AboutInfo]")
{
    for (const ThirdPartyComponent& component : thirdPartyComponents(sampleInfo())) {
        INFO(component.name.toStdString());
        REQUIRE_FALSE(component.license.isEmpty());
        REQUIRE(component.url.startsWith(QStringLiteral("https://")));
    }
}

TEST_CASE("formatAboutReport gathers every section under a heading", "[ui][AboutInfo]")
{
    const AboutInfo info = sampleInfo();
    const QString report = formatAboutReport(info);

    REQUIRE(report.contains(formatVersionInfo(info)));
    REQUIRE(report.contains(formatSystemInfo(info)));
    REQUIRE(report.contains(formatThirdPartyNotices(thirdPartyComponents(info))));
}

TEST_CASE("formatAboutReport names the project home page", "[ui][AboutInfo]")
{
    // The dialog shows it as a link, which a pasted bug report cannot carry.
    REQUIRE(formatAboutReport(sampleInfo()).contains(QString::fromLatin1(kProjectHomePage)));
    REQUIRE(QString::fromLatin1(kProjectHomePage) == QStringLiteral("https://www.slideio.com"));
}

TEST_CASE("formatAboutReport leaves out the licence body", "[ui][AboutInfo]")
{
    // The report is for pasting into a bug report. The licence is one tab away
    // for anyone who wants it.
    REQUIRE_FALSE(formatAboutReport(sampleInfo()).contains(QStringLiteral("REDISTRIBUTION")));
    REQUIRE_FALSE(formatAboutReport(sampleInfo()).contains(QStringLiteral("Redistribution and use")));
}

TEST_CASE("licenseText carries the BSD 3-Clause text embedded at configure time", "[ui][AboutInfo]")
{
    const QString text = licenseText();

    REQUIRE(text.contains(QStringLiteral("BSD 3-Clause License")));
    REQUIRE(text.contains(QStringLiteral("Redistribution and use")));
    REQUIRE(text.contains(QStringLiteral("Booritas")));
}

TEST_CASE("collectAboutInfo fills in what the running process knows", "[ui][AboutInfo]")
{
    GpuInfo gpu;
    gpu.renderer = QStringLiteral("test renderer");

    const AboutInfo info = collectAboutInfo(gpu);

    // The version comes from the generated build header, so it tracks the
    // project() version rather than a literal repeated here.
    REQUIRE_FALSE(info.appVersion.isEmpty());
    REQUIRE_FALSE(info.buildDate.isEmpty());
    REQUIRE_FALSE(info.osName.isEmpty());
    REQUIRE_FALSE(info.cpuArchitecture.isEmpty());
    REQUIRE_FALSE(info.qtRuntimeVersion.isEmpty());
    REQUIRE_FALSE(info.logFile.isEmpty());
    REQUIRE(info.gpu.renderer == QStringLiteral("test renderer"));
}
