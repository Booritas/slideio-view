#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/ColorProfilePolicy.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::SceneInfo makeScene(int index, int width, int height)
{
    core::SceneInfo s;
    s.index = index;
    s.width = width;
    s.height = height;
    s.numChannels = 3;
    return s;
}

// A minimal but structurally valid RGB ICC profile header: 128 header bytes
// with the size, 'RGB ' data space and 'acsp' signature in the right places,
// which is all inspectIccHeader reads.
std::vector<uint8_t> validRgbProfileBytes()
{
    std::vector<uint8_t> bytes(128, 0);
    const uint32_t size = 128;
    bytes[0] = static_cast<uint8_t>((size >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((size >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((size >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(size & 0xFF);
    bytes[16] = 'R'; bytes[17] = 'G'; bytes[18] = 'B'; bytes[19] = ' ';
    bytes[36] = 'a'; bytes[37] = 'c'; bytes[38] = 's'; bytes[39] = 'p';
    return bytes;
}

QString writeFile(const QString& path, const std::vector<uint8_t>& bytes)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
    f.close();
    return path;
}

} // namespace

TEST_CASE("no override falls back to the default", "[ui][ColorProfilePolicy]")
{
    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, {makeScene(0, 100, 100)}, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
    REQUIRE(problem.empty());
}

TEST_CASE("a matching override wins and is marked as one", "[ui][ColorProfilePolicy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString iccPath = writeFile(dir.filePath("slide.icc"), validRgbProfileBytes());

    const std::vector<core::SceneInfo> scenes{makeScene(0, 100, 100)};
    const std::string slideId = core::computeSlideId(scenes, 4096);

    ui::ColorProfilePolicy policy;
    policy.defaultBytes = {1, 2, 3};
    policy.overridePathsBySlideId[slideId] = iccPath.toStdString();

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, scenes, 4096, problem);

    REQUIRE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == validRgbProfileBytes());
    REQUIRE(problem.empty());
}

TEST_CASE("an override for a different slide does not apply",
          "[ui][ColorProfilePolicy]")
{
    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();
    policy.overridePathsBySlideId["ffffffffffffffff"] = "C:/profiles/other.icc";

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, {makeScene(0, 100, 100)}, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(problem.empty());
}

TEST_CASE("an empty scene list never matches an override",
          "[ui][ColorProfilePolicy]")
{
    // Review Focus 1. enumerateScenes returns empty for a file it cannot read.
    // Every such file hashes alike, so a lookup would apply one slide's
    // override to an unrelated one. The lookup must be declined outright.
    ui::ColorProfilePolicy policy;
    policy.defaultBytes = {9, 9, 9};
    // Spelled out rather than braced: an empty braced list is ambiguous between
    // the vector<SceneInfo> and SlideInfo overloads of computeSlideId.
    policy.overridePathsBySlideId[core::computeSlideId(std::vector<core::SceneInfo>{}, 0)] =
        "C:/profiles/x.icc";

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, {}, 0, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
}

TEST_CASE("an override is not applied via another slide's scene list",
          "[ui][ColorProfilePolicy]")
{
    // Guards the openScene/openAuxImage path: slideInfo.scenes describes the
    // previously open file, so an identity computed from it would be the wrong
    // slide's. Callers pass an empty list in that case, and an empty list must
    // never match a stored override.
    const std::vector<core::SceneInfo> otherSlideScenes{makeScene(0, 999, 777)};

    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();
    policy.overridePathsBySlideId[core::computeSlideId(otherSlideScenes, 1234)] =
        "C:/profiles/other.icc";

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, {}, 1234, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
    REQUIRE(problem.empty());
}

TEST_CASE("an unreadable override falls back and reports why",
          "[ui][ColorProfilePolicy]")
{
    // Review Focus 2, in its reachable form: a path that cannot be read must
    // not fail the open, and must not blame the slide.
    const std::vector<core::SceneInfo> scenes{makeScene(0, 100, 100)};

    // A nonexistent path under a real temporary directory, not a UNC share: the
    // branch under test is readable == false, and a share path would reach it
    // only after however long this host takes to give up on SMB discovery.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString missing = dir.filePath("no/such/profile.icc");

    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();
    policy.overridePathsBySlideId[core::computeSlideId(scenes, 4096)] = missing.toStdString();

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, scenes, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
    REQUIRE_FALSE(problem.empty());
    REQUIRE(problem.find("profile.icc") != std::string::npos);
}

TEST_CASE("an override that is not an RGB profile falls back",
          "[ui][ColorProfilePolicy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString junk = writeFile(dir.filePath("notaprofile.icc"),
                                   std::vector<uint8_t>(128, 0x7E));

    const std::vector<core::SceneInfo> scenes{makeScene(0, 100, 100)};

    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();
    policy.overridePathsBySlideId[core::computeSlideId(scenes, 4096)] = junk.toStdString();

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, scenes, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
    REQUIRE_FALSE(problem.empty());
}

TEST_CASE("a single-file slide sizes to that file's bytes", "[ui][ColorProfilePolicy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString file = writeFile(dir.filePath("slide.svs"), std::vector<uint8_t>(1234, 0x11));

    const auto size = ui::slideContentSize(file.toStdString());
    REQUIRE(size.has_value());
    REQUIRE(*size == 1234);
}

TEST_CASE("a directory slide sizes to the sum of the files beneath it",
          "[ui][ColorProfilePolicy]")
{
    // The DICOM case. std::filesystem::file_size does not report a directory's
    // contents -- on MSVC it does not even fail, it returns the directory
    // entry's own size, the same small constant for every study. Two studies
    // from one scanner share their scene geometry, so a constant size would
    // collide them onto one slide id and one stored override.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    writeFile(dir.filePath("a.dcm"), std::vector<uint8_t>(1000, 0x22));
    writeFile(dir.filePath("b.dcm"), std::vector<uint8_t>(2000, 0x33));
    REQUIRE(QDir(dir.path()).mkpath("sub"));
    writeFile(dir.filePath("sub/c.dcm"), std::vector<uint8_t>(3000, 0x44));

    const auto size = ui::slideContentSize(dir.path().toStdString());
    REQUIRE(size.has_value());
    REQUIRE(*size == 6000); // recursive, and nothing but the regular files
}

TEST_CASE("two directory slides of differing content size differently",
          "[ui][ColorProfilePolicy]")
{
    // The property that actually matters: the size must separate two studies
    // that a plain file_size would report identically.
    QTemporaryDir one;
    QTemporaryDir two;
    REQUIRE(one.isValid());
    REQUIRE(two.isValid());
    writeFile(one.filePath("a.dcm"), std::vector<uint8_t>(1000, 0x22));
    writeFile(two.filePath("a.dcm"), std::vector<uint8_t>(1001, 0x22));

    const auto sizeOne = ui::slideContentSize(one.path().toStdString());
    const auto sizeTwo = ui::slideContentSize(two.path().toStdString());
    REQUIRE(sizeOne.has_value());
    REQUIRE(sizeTwo.has_value());
    REQUIRE(*sizeOne != *sizeTwo);
}

TEST_CASE("a path that is not there has no size at all", "[ui][ColorProfilePolicy]")
{
    // nullopt, not zero: callers must decline to identify the slide rather than
    // hash every unsizable slide alike.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const auto size = ui::slideContentSize(dir.filePath("no/such/slide.svs").toStdString());
    REQUIRE_FALSE(size.has_value());
}

TEST_CASE("the DICOM study fixture sizes to its contents, not its directory entry",
          "[ui][ColorProfilePolicy]")
{
    // The plan-mandated fixture, and the case the helper exists for. A DICOM
    // study is a directory; std::filesystem::file_size reports its directory
    // entry instead of its contents, which is a small constant unrelated to the
    // study -- so two studies from one scanner, which already share their scene
    // geometry, would collide onto one slide id.
    //
    // Skipped rather than failed when the corpus is absent, matching how the
    // infra suite handles the same problem.
    const char* root = std::getenv("SLIDEIO_VIEWER_TEST_IMAGES");
    if (!root) {
        SKIP("SLIDEIO_VIEWER_TEST_IMAGES is not set");
    }
    const std::string study = std::string(root) + "/dcm/benigns_01/patient0186";
    if (!QDir(QString::fromStdString(study)).exists()) {
        SKIP("the DICOM study fixture is not present");
    }

    const auto size = ui::slideContentSize(study);
    REQUIRE(size.has_value());

    // Asserted as a floor rather than an exact byte count so the test survives
    // a corpus refresh. The point is that it is the study's bytes and not a
    // directory entry, which is at most a few kilobytes.
    REQUIRE(*size > 1024 * 1024);
}
