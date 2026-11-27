#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/BuiltLevelCache.h"
#include "slideio/viewer/infra/BuiltLevelDiskCache.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::BuiltLevelCacheKey makeKey(core::DataType type, int channels)
{
    core::BuiltLevelCacheKey key;
    key.slideId = "0123456789abcdef";
    key.sceneIndex = 2;
    key.fileSize = 12345678901234ULL;
    key.fileModifiedMs = 1700000000123LL;
    key.sourceLevel = 0;
    key.sourceWidth = 4096;
    key.sourceHeight = 3000;
    key.numChannels = channels;
    key.dataType = type;
    key.residentFactors = {4, 8};
    key.colorMode = core::ColorMode::Managed;
    key.profileFingerprint = "00112233aabbccdd";
    key.renditionTag = "srgb/perceptual";
    key.slideioVersion = "2.0.0";
    return key;
}

core::BuiltLevelCacheDescription makeDescription()
{
    core::BuiltLevelCacheDescription d;
    d.path = "C:/slides/a.svs";
    d.fileName = "a.svs";
    d.driverId = "SVS";
    d.sceneName = "scene";
    d.width = 4096;
    d.height = 3000;
    d.magnification = 20.0;
    d.pixelSizeXUm = 0.5;
    d.pixelSizeYUm = 0.5;
    d.fileSize = 12345678901234ULL;
    d.fileModifiedMs = 1700000000123LL;
    return d;
}

struct Rasters
{
    std::vector<std::pair<int, int>> sizes;
    std::vector<std::vector<uint8_t>> data;
    std::vector<core::BuiltLevelView> views() const
    {
        std::vector<core::BuiltLevelView> v;
        for (size_t i = 0; i < data.size(); ++i) {
            v.push_back({sizes[i].first, sizes[i].second, data[i].data(), data[i].size()});
        }
        return v;
    }
};

Rasters makeRasters(size_t pixelBytes, int w0 = 40, int h0 = 30)
{
    Rasters r;
    r.sizes = {{w0, h0}, {w0 / 2, h0 / 2}};
    for (const auto& s : r.sizes) {
        std::vector<uint8_t> d(static_cast<size_t>(s.first) * s.second * pixelBytes);
        for (size_t i = 0; i < d.size(); ++i) {
            d[i] = static_cast<uint8_t>((i * 7 + i / 13) & 0xFF);
        }
        r.data.push_back(std::move(d));
    }
    return r;
}

std::string utf8(const QString& s)
{
    return s.toStdString();
}

QString entryDir(const std::string& folder, const core::BuiltLevelCacheKey& key)
{
    return QString::fromStdString(folder) + "/" + QString::fromStdString(core::builtLevelCacheKeyName(key));
}

QStringList subdirs(const std::string& folder)
{
    return QDir(QString::fromStdString(folder)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

void roundTrip(core::DataType type, int channels, size_t pixelBytes)
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    infra::BuiltLevelDiskCache cache(utf8(tmp.path()));
    const auto key = makeKey(type, channels);
    const auto rasters = makeRasters(pixelBytes);
    std::atomic<bool> cancel{false};
    REQUIRE(cache.save(key, makeDescription(), rasters.views(), cancel));
    const auto loaded = cache.load(key, rasters.sizes, pixelBytes);
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == rasters.data);
}

} // namespace

TEST_CASE("save then load round-trips", "[BuiltLevelDiskCache]")
{
    SECTION("Byte 3 channels") { roundTrip(core::DataType::Byte, 3, 3); }
    SECTION("UInt16 1 channel") { roundTrip(core::DataType::UInt16, 1, 2); }
    SECTION("Float32 2 channels") { roundTrip(core::DataType::Float32, 2, 8); }
}

TEST_CASE("a non-ASCII folder round-trips", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // Spelled as bytes: the project does not pass /utf-8 to MSVC.
    const std::string folder = utf8(tmp.path()) + "/J\xC3\xB6rg \xC3\x9Cn\xC3\xAF" "code/cache";
    infra::BuiltLevelDiskCache cache(folder);
    const auto key = makeKey(core::DataType::Byte, 3);
    const auto rasters = makeRasters(3);
    std::atomic<bool> cancel{false};
    REQUIRE(cache.save(key, makeDescription(), rasters.views(), cancel));
    const auto loaded = cache.load(key, rasters.sizes, 3);
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == rasters.data);
}

TEST_CASE("a block always holds whole rows", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    infra::BuiltLevelDiskCache cache(utf8(tmp.path()), 1);
    const auto key = makeKey(core::DataType::Byte, 3);
    Rasters r;
    r.sizes = {{1000, 7}, {500, 3}};
    for (const auto& s : r.sizes) {
        std::vector<uint8_t> d(static_cast<size_t>(s.first) * s.second * 3);
        for (size_t i = 0; i < d.size(); ++i) {
            d[i] = static_cast<uint8_t>(i * 31);
        }
        r.data.push_back(std::move(d));
    }
    std::atomic<bool> cancel{false};
    REQUIRE(cache.save(key, makeDescription(), r.views(), cancel));
    const auto loaded = cache.load(key, r.sizes, 3);
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == r.data);

    QFile f(entryDir(utf8(tmp.path()), key) + "/level-0.bin");
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    REQUIRE(bytes.size() >= 12);
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.constData());
    const uint32_t blocks = static_cast<uint32_t>(p[8]) | (static_cast<uint32_t>(p[9]) << 8)
                            | (static_cast<uint32_t>(p[10]) << 16) | (static_cast<uint32_t>(p[11]) << 24);
    REQUIRE(blocks == 7u);
}

TEST_CASE("each corruption is a miss", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::string folder = utf8(tmp.path());
    infra::BuiltLevelDiskCache cache(folder);
    const auto key = makeKey(core::DataType::Byte, 3);
    const auto rasters = makeRasters(3);
    std::atomic<bool> cancel{false};
    const auto dir = entryDir(folder, key);

    REQUIRE(cache.save(key, makeDescription(), rasters.views(), cancel));
    REQUIRE(cache.load(key, rasters.sizes, 3).has_value());

    SECTION("truncated level file")
    {
        QFile f(dir + "/level-1.bin");
        REQUIRE(f.resize(f.size() - 10));
        REQUIRE_FALSE(cache.load(key, rasters.sizes, 3).has_value());
    }
    SECTION("flipped byte")
    {
        QFile f(dir + "/level-0.bin");
        REQUIRE(f.open(QIODevice::ReadWrite));
        const qint64 mid = f.size() / 2;
        f.seek(mid);
        char c = 0;
        f.read(&c, 1);
        f.seek(mid);
        c = static_cast<char>(c ^ 0x5A);
        f.write(&c, 1);
        f.close();
        REQUIRE_FALSE(cache.load(key, rasters.sizes, 3).has_value());
    }
    SECTION("header field changed")
    {
        QFile f(dir + "/header.json");
        REQUIRE(f.open(QIODevice::ReadOnly));
        QByteArray text = f.readAll();
        f.close();
        REQUIRE(text.contains("\"sceneIndex\":2"));
        text.replace("\"sceneIndex\":2", "\"sceneIndex\":3");
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        f.close();
        REQUIRE_FALSE(cache.load(key, rasters.sizes, 3).has_value());
    }
    SECTION("missing level file")
    {
        REQUIRE(QFile::remove(dir + "/level-1.bin"));
        REQUIRE_FALSE(cache.load(key, rasters.sizes, 3).has_value());
    }
    SECTION("different slideio version")
    {
        auto other = key;
        other.slideioVersion = "2.0.1";
        REQUIRE_FALSE(cache.load(other, rasters.sizes, 3).has_value());
    }
    SECTION("wrong expected size")
    {
        auto sizes = rasters.sizes;
        sizes[1].first += 1;
        REQUIRE_FALSE(cache.load(key, sizes, 3).has_value());
    }
}

TEST_CASE("cancel leaves nothing behind", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::string folder = utf8(tmp.path());
    infra::BuiltLevelDiskCache cache(folder);
    const auto rasters = makeRasters(3);
    std::atomic<bool> cancel{true};
    REQUIRE_FALSE(cache.save(makeKey(core::DataType::Byte, 3), makeDescription(), rasters.views(), cancel));
    REQUIRE(subdirs(folder).isEmpty());
}

TEST_CASE("racing saves leave one valid entry", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::string folder = utf8(tmp.path());
    infra::BuiltLevelDiskCache a(folder);
    infra::BuiltLevelDiskCache b(folder);
    const auto key = makeKey(core::DataType::Byte, 3);
    const auto rasters = makeRasters(3);
    std::atomic<bool> cancel{false};
    bool okA = false;
    bool okB = false;
    std::thread ta([&] { okA = a.save(key, makeDescription(), rasters.views(), cancel); });
    std::thread tb([&] { okB = b.save(key, makeDescription(), rasters.views(), cancel); });
    ta.join();
    tb.join();
    REQUIRE(okA);
    REQUIRE(okB);
    const auto dirs = subdirs(folder);
    REQUIRE(dirs.size() == 1);
    REQUIRE_FALSE(dirs[0].contains(".tmp-"));
    const auto loaded = a.load(key, rasters.sizes, 3);
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == rasters.data);
}

TEST_CASE("an unwritable folder fails cleanly", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString file = tmp.path() + "/not-a-folder";
    QFile f(file);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    infra::BuiltLevelDiskCache cache(utf8(file));
    const auto rasters = makeRasters(3);
    std::atomic<bool> cancel{false};
    bool ok = true;
    REQUIRE_NOTHROW(ok = cache.save(makeKey(core::DataType::Byte, 3), makeDescription(), rasters.views(), cancel));
    REQUIRE_FALSE(ok);
}

TEST_CASE("sweep deletes only expired entries and stale temporaries", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::string folder = utf8(tmp.path());
    const int64_t now = 1800000000;
    auto makeEntry = [&](const QString& name, int64_t lastUsed) {
        QDir().mkpath(tmp.path() + "/" + name);
        QFile f(tmp.path() + "/" + name + "/last-used");
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray::number(static_cast<qlonglong>(lastUsed)));
    };
    const QString old = QString(32, 'a');
    const QString recent = QString(32, 'b');
    const QString temp = QString(32, 'c') + ".tmp-deadbeef";
    makeEntry(old, now - 31 * 86400);
    makeEntry(recent, now - 29 * 86400);
    makeEntry(temp, now - 2 * 86400);
    makeEntry("keep-me", now - 400 * 86400);
    QFile notes(tmp.path() + "/notes.txt");
    REQUIRE(notes.open(QIODevice::WriteOnly));
    notes.write("n");
    notes.close();

    REQUIRE(infra::BuiltLevelDiskCache::sweep(folder, 30, now) == 2);
    REQUIRE_FALSE(QDir(tmp.path() + "/" + old).exists());
    REQUIRE(QDir(tmp.path() + "/" + recent).exists());
    REQUIRE_FALSE(QDir(tmp.path() + "/" + temp).exists());
    REQUIRE(QDir(tmp.path() + "/keep-me").exists());
    REQUIRE(QFile::exists(tmp.path() + "/notes.txt"));
}

TEST_CASE("load marks the entry used", "[BuiltLevelDiskCache]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::string folder = utf8(tmp.path());
    infra::BuiltLevelDiskCache cache(folder);
    const auto key = makeKey(core::DataType::Byte, 3);
    const auto rasters = makeRasters(3);
    std::atomic<bool> cancel{false};
    REQUIRE(cache.save(key, makeDescription(), rasters.views(), cancel));
    const QString lastUsed = entryDir(folder, key) + "/last-used";
    {
        QFile f(lastUsed);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("1");
    }
    const auto start = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    REQUIRE(cache.load(key, rasters.sizes, 3).has_value());
    QFile f(lastUsed);
    REQUIRE(f.open(QIODevice::ReadOnly));
    REQUIRE(f.readAll().trimmed().toLongLong() >= start);
}
