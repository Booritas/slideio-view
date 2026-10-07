#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/QSettingsColorProfileOverrideStore.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

using namespace slideio::viewer::core;
using slideio::viewer::infra::QSettingsColorProfileOverrideStore;

namespace
{

ColorProfileOverride makeEntry(const std::string& slideId)
{
    ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = "C:/profiles/scanner.icc";
    entry.slideDisplayName = "case001_HE.svs";
    entry.displacedEmbedded = true;
    return entry;
}

} // namespace

TEST_CASE("QSettings store round-trips every field", "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("overrides.ini");

    {
        QSettingsColorProfileOverrideStore store(ini);
        store.set(makeEntry("a3f8c2e109b74d21"));
    }

    // A second store over the same file: this is the across-sessions claim.
    QSettingsColorProfileOverrideStore reopened(ini);
    const auto found = reopened.find("a3f8c2e109b74d21");

    REQUIRE(found.has_value());
    REQUIRE(found->slideId == "a3f8c2e109b74d21");
    REQUIRE(found->profilePath == "C:/profiles/scanner.icc");
    REQUIRE(found->slideDisplayName == "case001_HE.svs");
    REQUIRE(found->displacedEmbedded);
}

TEST_CASE("QSettings store reports a miss as empty", "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("overrides.ini"));

    REQUIRE_FALSE(store.find("0000000000000000").has_value());
}

TEST_CASE("QSettings store removes one entry and lists the rest",
          "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("overrides.ini"));

    store.set(makeEntry("aaaaaaaaaaaaaaaa"));
    store.set(makeEntry("bbbbbbbbbbbbbbbb"));
    store.remove("aaaaaaaaaaaaaaaa");

    REQUIRE_FALSE(store.find("aaaaaaaaaaaaaaaa").has_value());
    REQUIRE(store.all().size() == 1);
    REQUIRE(store.all().front().slideId == "bbbbbbbbbbbbbbbb");
}

TEST_CASE("QSettings store overwrites rather than duplicating",
          "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("overrides.ini"));

    store.set(makeEntry("a3f8c2e109b74d21"));
    ColorProfileOverride second = makeEntry("a3f8c2e109b74d21");
    second.profilePath = "C:/profiles/other.icc";
    second.displacedEmbedded = false;
    store.set(second);

    REQUIRE(store.all().size() == 1);
    REQUIRE(store.find("a3f8c2e109b74d21")->profilePath == "C:/profiles/other.icc");
    REQUIRE_FALSE(store.find("a3f8c2e109b74d21")->displacedEmbedded);
}

TEST_CASE("QSettings store starts empty on a fresh file", "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("nothing-here.ini"));

    REQUIRE(store.all().empty());
}
