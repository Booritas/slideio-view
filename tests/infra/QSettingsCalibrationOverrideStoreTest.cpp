#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/QSettingsCalibrationOverrideStore.h"

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

using namespace slideio::viewer::core;
using slideio::viewer::infra::QSettingsCalibrationOverrideStore;

namespace
{

const std::string kSlide = "a3f8c2e109b74d21";

CalibrationOverride makeEntry(int scene, PixelSize value)
{
    CalibrationOverride entry;
    entry.slideId = kSlide;
    entry.sceneIndex = scene;
    entry.micronsPerPixel = value;
    entry.slideDisplayName = "case001.svs";
    return entry;
}

} // namespace

TEST_CASE("an override survives a second store over the same file", "[infra][CalibrationStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("calibration.ini").toStdString();
    {
        QSettingsCalibrationOverrideStore store(ini);
        REQUIRE(store.set(makeEntry(0, PixelSize{0.5, 0.25})));
    }
    QSettingsCalibrationOverrideStore reopened(ini);
    const auto found = reopened.find(kSlide, 0);
    REQUIRE(found.has_value());
    CHECK(found->micronsPerPixel.x == 0.5);
    CHECK(found->micronsPerPixel.y == 0.25);
    CHECK(found->slideDisplayName == "case001.svs");
    CHECK(found->sceneIndex == 0);
}

TEST_CASE("an override is per scene", "[infra][CalibrationStore]")
{
    QTemporaryDir dir;
    QSettingsCalibrationOverrideStore store(dir.filePath("calibration.ini").toStdString());
    REQUIRE(store.set(makeEntry(0, PixelSize{0.5, 0.5})));
    CHECK_FALSE(store.find(kSlide, 1).has_value());
    REQUIRE(store.set(makeEntry(1, PixelSize{0.3, 0.3})));
    REQUIRE(store.all().size() == 2);
    CHECK(store.find(kSlide, 0)->micronsPerPixel.x == 0.5);
    CHECK(store.find(kSlide, 1)->micronsPerPixel.x == 0.3);
}

TEST_CASE("remove forgets an override", "[infra][CalibrationStore]")
{
    QTemporaryDir dir;
    QSettingsCalibrationOverrideStore store(dir.filePath("calibration.ini").toStdString());
    REQUIRE(store.set(makeEntry(0, PixelSize{0.5, 0.5})));
    store.remove(kSlide, 0);
    CHECK_FALSE(store.find(kSlide, 0).has_value());
    CHECK(store.all().empty());
}

TEST_CASE("set refuses an empty slide id and an invalid value", "[infra][CalibrationStore]")
{
    QTemporaryDir dir;
    QSettingsCalibrationOverrideStore store(dir.filePath("calibration.ini").toStdString());
    CalibrationOverride unidentified = makeEntry(0, PixelSize{0.5, 0.5});
    unidentified.slideId.clear();
    CHECK_FALSE(store.set(unidentified));
    CHECK_FALSE(store.set(makeEntry(0, PixelSize{0.0, 0.5})));
    CHECK_FALSE(store.set(makeEntry(0, PixelSize{250.0, 250.0})));
    CHECK(store.all().empty());
}

TEST_CASE("a hand-edited invalid value is not returned", "[infra][CalibrationStore]")
{
    QTemporaryDir dir;
    const QString ini = dir.filePath("calibration.ini");
    QSettingsCalibrationOverrideStore store(ini.toStdString());
    REQUIRE(store.set(makeEntry(0, PixelSize{0.5, 0.5})));
    {
        QSettings raw(ini, QSettings::IniFormat);
        raw.setValue(QString("calibration/overrides/%1.s0/x").arg(QString::fromStdString(kSlide)), 0.0);
        raw.sync();
    }
    QSettingsCalibrationOverrideStore reopened(ini.toStdString());
    CHECK_FALSE(reopened.find(kSlide, 0).has_value());
    CHECK(reopened.all().empty());
}

TEST_CASE("find with an empty slide id finds nothing", "[infra][CalibrationStore]")
{
    QTemporaryDir dir;
    QSettingsCalibrationOverrideStore store(dir.filePath("calibration.ini").toStdString());
    CHECK_FALSE(store.find("", 0).has_value());
}
