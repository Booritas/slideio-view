#include "slideio/viewer/infra/BuiltLevelDiskCache.h"

#include "slideio/viewer/core/Iso8601.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QString>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <utility>

namespace slideio::viewer::infra
{

namespace
{

constexpr char kMagic[4] = {'S', 'V', 'B', 'L'};
constexpr int kFileHeaderBytes = 12;
constexpr int kBlockHeaderBytes = 12;

void putU32(QByteArray& out, uint32_t v)
{
    const char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                       static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    out.append(b, 4);
}

uint32_t getU32(const char* p)
{
    const auto* u = reinterpret_cast<const uint8_t*>(p);
    return static_cast<uint32_t>(u[0]) | (static_cast<uint32_t>(u[1]) << 8) | (static_cast<uint32_t>(u[2]) << 16)
           | (static_cast<uint32_t>(u[3]) << 24);
}

int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool writeAll(QFile& f, const QByteArray& bytes)
{
    return f.write(bytes) == bytes.size();
}

bool writeLastUsed(const QString& entryDir)
{
    QFile f(entryDir + QStringLiteral("/last-used"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const bool ok = writeAll(f, QByteArray::number(static_cast<qlonglong>(nowSeconds())));
    f.close();
    return ok && f.error() == QFileDevice::NoError;
}

QJsonObject keyToJson(const core::BuiltLevelCacheKey& key)
{
    QJsonArray factors;
    for (const int f : key.residentFactors) {
        factors.append(f);
    }
    QJsonObject o;
    o.insert(QStringLiteral("slideId"), QString::fromStdString(key.slideId));
    o.insert(QStringLiteral("driverId"), QString::fromStdString(key.driverId));
    o.insert(QStringLiteral("sceneIndex"), key.sceneIndex);
    o.insert(QStringLiteral("fileSize"), QString::number(static_cast<qulonglong>(key.fileSize)));
    o.insert(QStringLiteral("fileModifiedMs"), QString::number(static_cast<qlonglong>(key.fileModifiedMs)));
    o.insert(QStringLiteral("sourceLevel"), key.sourceLevel);
    o.insert(QStringLiteral("sourceWidth"), key.sourceWidth);
    o.insert(QStringLiteral("sourceHeight"), key.sourceHeight);
    o.insert(QStringLiteral("numChannels"), key.numChannels);
    o.insert(QStringLiteral("dataType"), static_cast<int>(key.dataType));
    o.insert(QStringLiteral("residentFactors"), factors);
    o.insert(QStringLiteral("colorMode"), static_cast<int>(key.colorMode));
    o.insert(QStringLiteral("profileFingerprint"), QString::fromStdString(key.profileFingerprint));
    o.insert(QStringLiteral("renditionTag"), QString::fromStdString(key.renditionTag));
    o.insert(QStringLiteral("slideioVersion"), QString::fromStdString(key.slideioVersion));
    o.insert(QStringLiteral("formatVersion"), key.formatVersion);
    return o;
}

bool jsonInt(const QJsonObject& o, const char* name, int& out)
{
    const QJsonValue v = o.value(QLatin1String(name));
    if (!v.isDouble()) {
        return false;
    }
    out = v.toInt();
    return static_cast<double>(out) == v.toDouble();
}

bool jsonString(const QJsonObject& o, const char* name, std::string& out)
{
    const QJsonValue v = o.value(QLatin1String(name));
    if (!v.isString()) {
        return false;
    }
    out = v.toString().toStdString();
    return true;
}

bool jsonU64(const QJsonObject& o, const char* name, uint64_t& out)
{
    const QJsonValue v = o.value(QLatin1String(name));
    if (!v.isString()) {
        return false;
    }
    bool ok = false;
    out = v.toString().toULongLong(&ok);
    return ok;
}

bool jsonI64(const QJsonObject& o, const char* name, int64_t& out)
{
    const QJsonValue v = o.value(QLatin1String(name));
    if (!v.isString()) {
        return false;
    }
    bool ok = false;
    out = v.toString().toLongLong(&ok);
    return ok;
}

/// Reads every key field back and builds a key to compare with operator==.
bool keyFromJson(const QJsonObject& o, core::BuiltLevelCacheKey& key)
{
    int dataType = 0;
    int colorMode = 0;
    if (!jsonString(o, "slideId", key.slideId) || !jsonString(o, "driverId", key.driverId)
        || !jsonInt(o, "sceneIndex", key.sceneIndex)
        || !jsonU64(o, "fileSize", key.fileSize) || !jsonI64(o, "fileModifiedMs", key.fileModifiedMs)
        || !jsonInt(o, "sourceLevel", key.sourceLevel) || !jsonInt(o, "sourceWidth", key.sourceWidth)
        || !jsonInt(o, "sourceHeight", key.sourceHeight) || !jsonInt(o, "numChannels", key.numChannels)
        || !jsonInt(o, "dataType", dataType) || !jsonInt(o, "colorMode", colorMode)
        || !jsonString(o, "profileFingerprint", key.profileFingerprint)
        || !jsonString(o, "renditionTag", key.renditionTag) || !jsonString(o, "slideioVersion", key.slideioVersion)
        || !jsonInt(o, "formatVersion", key.formatVersion)) {
        return false;
    }
    key.dataType = static_cast<core::DataType>(dataType);
    key.colorMode = static_cast<core::ColorMode>(colorMode);
    const QJsonValue factors = o.value(QStringLiteral("residentFactors"));
    if (!factors.isArray()) {
        return false;
    }
    key.residentFactors.clear();
    for (const QJsonValue f : factors.toArray()) {
        if (!f.isDouble()) {
            return false;
        }
        key.residentFactors.push_back(f.toInt());
    }
    return true;
}

QString joinPath(const std::string& folder, const std::string& name)
{
    return QString::fromStdString(folder) + QLatin1Char('/') + QString::fromStdString(name);
}

/// "<key name>.tmp-<8 random hex>": a directory the sweep recognises as temporary.
QString temporaryName(const std::string& keyName)
{
    return QString::fromStdString(keyName) + QStringLiteral(".tmp-")
           + QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, '0');
}

/// Removes a temporary directory; reports nothing.
void discard(const QString& dir)
{
    QDir(dir).removeRecursively();
}

bool looksValid(const QString& entryDir)
{
    QFile f(entryDir + QStringLiteral("/header.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() && doc.object().contains(QStringLiteral("key"));
}

} // namespace

BuiltLevelDiskCache::BuiltLevelDiskCache(std::string folderUtf8, size_t blockBytes)
    : m_folder(std::move(folderUtf8))
    , m_blockBytes(blockBytes)
{
}

std::optional<std::vector<std::vector<uint8_t>>> BuiltLevelDiskCache::load(
    const core::BuiltLevelCacheKey& key, const std::vector<std::pair<int, int>>& levelSizes, size_t pixelBytes,
    const std::atomic<bool>& cancel)
{
    const std::string name = core::builtLevelCacheKeyName(key);
    const QString dir = joinPath(m_folder, name);
    std::string describedPath;

    using Rasters = std::optional<std::vector<std::vector<uint8_t>>>;
    std::string reason;
    bool absent = false;
    // A cancelled read says nothing about the entry: it is neither set aside
    // nor reported as a miss.
    bool cancelled = false;

    auto miss = [&](const char* why) -> Rasters {
        reason = why;
        return std::nullopt;
    };
    auto stop = [&]() -> Rasters {
        cancelled = true;
        return std::nullopt;
    };

    // Every file handle lives inside this lambda, so none is open when a
    // failed entry is moved aside below.
    auto attempt = [&]() -> Rasters {
        if (cancel.load()) {
            return stop();
        }
        if (!QFileInfo(dir).isDir()) {
            absent = true;
            spdlog::debug("Level cache: no entry {}", name);
            return std::nullopt;
        }
        QFile headerFile(dir + QStringLiteral("/header.json"));
        if (!headerFile.open(QIODevice::ReadOnly)) {
            return miss("header.json cannot be opened");
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(headerFile.readAll(), &parseError);
        if (!doc.isObject()) {
            return miss("header.json is not valid JSON");
        }
        const QJsonObject header = doc.object();
        const QJsonValue description = header.value(QStringLiteral("description"));
        if (description.isObject() && description.toObject().value(QStringLiteral("path")).isString()) {
            describedPath = description.toObject().value(QStringLiteral("path")).toString().toStdString();
        }

        core::BuiltLevelCacheKey stored;
        if (!header.value(QStringLiteral("key")).isObject()
            || !keyFromJson(header.value(QStringLiteral("key")).toObject(), stored)) {
            return miss("the stored key is unreadable");
        }
        if (stored != key) {
            return miss("the stored key differs from the requested one");
        }

        const QJsonValue levelsValue = header.value(QStringLiteral("levels"));
        if (!levelsValue.isArray() || static_cast<size_t>(levelsValue.toArray().size()) != levelSizes.size()) {
            return miss("the level count differs");
        }
        std::vector<size_t> expectedBytes;
        const QJsonArray levelsArray = levelsValue.toArray();
        for (size_t i = 0; i < levelSizes.size(); ++i) {
            const QJsonObject l = levelsArray.at(static_cast<int>(i)).toObject();
            int w = 0;
            int h = 0;
            if (!jsonInt(l, "width", w) || !jsonInt(l, "height", h) || w != levelSizes[i].first
                || h != levelSizes[i].second || w <= 0 || h <= 0) {
                return miss("a level size differs");
            }
            const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * pixelBytes;
            if (!l.value(QStringLiteral("bytes")).isDouble()
                || l.value(QStringLiteral("bytes")).toDouble() != static_cast<double>(bytes)) {
                return miss("a level byte count differs");
            }
            expectedBytes.push_back(bytes);
        }

        std::vector<std::vector<uint8_t>> result;
        for (size_t i = 0; i < levelSizes.size(); ++i) {
            QFile f(dir + QStringLiteral("/level-%1.bin").arg(i));
            if (!f.open(QIODevice::ReadOnly)) {
                return miss("a level file is missing");
            }
            QByteArray head = f.read(kFileHeaderBytes);
            if (head.size() != kFileHeaderBytes || std::memcmp(head.constData(), kMagic, 4) != 0) {
                return miss("bad magic");
            }
            if (getU32(head.constData() + 4) != static_cast<uint32_t>(core::kBuiltLevelCacheFormatVersion)) {
                return miss("a level file has another format version");
            }
            const uint32_t blockCount = getU32(head.constData() + 8);
            std::vector<uint8_t> raster(expectedBytes[i]);
            size_t filled = 0;
            for (uint32_t b = 0; b < blockCount; ++b) {
                if (cancel.load()) {
                    return stop();
                }
                const QByteArray bh = f.read(kBlockHeaderBytes);
                if (bh.size() != kBlockHeaderBytes) {
                    return miss("the block count is short");
                }
                const uint32_t usize = getU32(bh.constData());
                const uint32_t csize = getU32(bh.constData() + 4);
                const uint32_t crc = getU32(bh.constData() + 8);
                if (usize > expectedBytes[i] - filled || static_cast<qint64>(csize) > f.size() - f.pos()) {
                    return miss("a block size is out of range");
                }
                const QByteArray compressed = f.read(csize);
                if (static_cast<uint32_t>(compressed.size()) != csize) {
                    return miss("a block is truncated");
                }
                // qCompress prefixes its stream with the uncompressed length,
                // big-endian. Checked first so a corrupt prefix is a miss here
                // rather than an allocation of that size, or a qWarning, inside
                // qUncompress.
                if (csize < 4) {
                    return miss("a block is shorter than its length prefix");
                }
                const auto* prefix = reinterpret_cast<const uint8_t*>(compressed.constData());
                const uint32_t declared = (static_cast<uint32_t>(prefix[0]) << 24)
                                          | (static_cast<uint32_t>(prefix[1]) << 16)
                                          | (static_cast<uint32_t>(prefix[2]) << 8) | static_cast<uint32_t>(prefix[3]);
                if (declared != usize) {
                    return miss("a block's length prefix differs from its header");
                }
                const QByteArray plain = qUncompress(compressed);
                if (static_cast<uint32_t>(plain.size()) != usize) {
                    return miss("a block decompresses to another size");
                }
                if (core::crc32(reinterpret_cast<const uint8_t*>(plain.constData()), plain.size()) != crc) {
                    return miss("a block fails its checksum");
                }
                std::memcpy(raster.data() + filled, plain.constData(), plain.size());
                filled += static_cast<size_t>(plain.size());
            }
            if (filled != expectedBytes[i]) {
                return miss("a level holds the wrong number of bytes");
            }
            if (!f.atEnd()) {
                return miss("a level file has trailing bytes");
            }
            result.push_back(std::move(raster));
        }

        if (!writeLastUsed(dir)) {
            spdlog::debug("Level cache: could not update last-used of {}", name);
        }
        return result;
    };

    try {
        if (Rasters hit = attempt()) {
            return hit;
        }
    } catch (const std::exception& e) {
        reason = e.what();
    } catch (...) {
        reason = "unknown error";
    }
    if (absent) {
        return std::nullopt;
    }
    if (cancelled) {
        spdlog::debug("Level cache: load of {} cancelled", name);
        return std::nullopt;
    }
    // Move the failed entry aside so the next save's rename can succeed; the
    // sweep removes the aside copy. Failing to do so (a file open elsewhere)
    // leaves the entry where it was.
    bool asideDone = false;
    try {
        asideDone = QDir(QString::fromStdString(m_folder)).rename(QString::fromStdString(name), temporaryName(name));
    } catch (...) {
    }
    spdlog::info("Level cache miss for {}: {} (slide '{}'; entry {})", name, reason, describedPath,
                 asideDone ? "set aside" : "left in place");
    return std::nullopt;
}

bool BuiltLevelDiskCache::save(const core::BuiltLevelCacheKey& key, const core::BuiltLevelCacheDescription& description,
                               const std::vector<core::BuiltLevelView>& levels, const std::atomic<bool>& cancel)
{
    const std::string name = core::builtLevelCacheKeyName(key);
    const QString folder = QString::fromStdString(m_folder);
    const QString finalDir = joinPath(m_folder, name);
    const QString tempName = temporaryName(name);
    const QString tempDir = folder + QLatin1Char('/') + tempName;
    bool tempCreated = false;

    auto fail = [&](const QString& why) {
        spdlog::warn("Level cache: could not save {}: {}", name, why.toStdString());
        if (tempCreated) {
            discard(tempDir);
        }
        return false;
    };

    try {
        if (levels.empty()) {
            return fail(QStringLiteral("no levels"));
        }
        if (!QDir().mkpath(folder)) {
            return fail(QStringLiteral("the cache folder cannot be created"));
        }
        if (!QDir().mkdir(tempDir)) {
            return fail(QStringLiteral("the temporary folder cannot be created"));
        }
        tempCreated = true;

        QJsonArray levelsJson;
        for (const auto& l : levels) {
            const size_t pixels = static_cast<size_t>(std::max(l.width, 0)) * static_cast<size_t>(std::max(l.height, 0));
            if (pixels == 0 || l.data == nullptr || l.size == 0 || l.size % pixels != 0) {
                return fail(QStringLiteral("a level has an inconsistent size"));
            }
            QJsonObject o;
            o.insert(QStringLiteral("width"), l.width);
            o.insert(QStringLiteral("height"), l.height);
            o.insert(QStringLiteral("bytes"), static_cast<double>(l.size));
            levelsJson.append(o);
        }

        QJsonObject desc;
        desc.insert(QStringLiteral("path"), QString::fromStdString(description.path));
        desc.insert(QStringLiteral("fileName"), QString::fromStdString(description.fileName));
        desc.insert(QStringLiteral("driverId"), QString::fromStdString(description.driverId));
        desc.insert(QStringLiteral("sceneName"), QString::fromStdString(description.sceneName));
        desc.insert(QStringLiteral("sceneIndex"), description.sceneIndex);
        desc.insert(QStringLiteral("width"), description.width);
        desc.insert(QStringLiteral("height"), description.height);
        desc.insert(QStringLiteral("magnification"), description.magnification);
        desc.insert(QStringLiteral("pixelSizeXUm"), description.pixelSizeXUm);
        desc.insert(QStringLiteral("pixelSizeYUm"), description.pixelSizeYUm);
        desc.insert(QStringLiteral("fileSize"), QString::number(static_cast<qulonglong>(description.fileSize)));
        desc.insert(QStringLiteral("fileModifiedMs"), QString::number(static_cast<qlonglong>(description.fileModifiedMs)));
        desc.insert(QStringLiteral("writtenAt"),
                    QString::fromStdString(core::formatIso8601Utc(std::chrono::system_clock::now())));

        QJsonObject header;
        header.insert(QStringLiteral("formatVersion"), core::kBuiltLevelCacheFormatVersion);
        header.insert(QStringLiteral("key"), keyToJson(key));
        header.insert(QStringLiteral("levels"), levelsJson);
        header.insert(QStringLiteral("description"), desc);

        {
            QFile f(tempDir + QStringLiteral("/header.json"));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || !writeAll(f, QJsonDocument(header).toJson(QJsonDocument::Compact))) {
                return fail(QStringLiteral("header.json cannot be written"));
            }
            f.close();
            if (f.error() != QFileDevice::NoError) {
                return fail(QStringLiteral("header.json cannot be written"));
            }
        }

        for (size_t i = 0; i < levels.size(); ++i) {
            const auto& l = levels[i];
            const size_t rowBytes = l.size / static_cast<size_t>(l.height);
            const size_t rowsPerBlock = std::max<size_t>(1, m_blockBytes / rowBytes);
            const size_t blockCount = (static_cast<size_t>(l.height) + rowsPerBlock - 1) / rowsPerBlock;

            QFile f(tempDir + QStringLiteral("/level-%1.bin").arg(i));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return fail(QStringLiteral("a level file cannot be created"));
            }
            QByteArray head(kMagic, 4);
            putU32(head, static_cast<uint32_t>(core::kBuiltLevelCacheFormatVersion));
            putU32(head, static_cast<uint32_t>(blockCount));
            if (!writeAll(f, head)) {
                return fail(QStringLiteral("a level file cannot be written"));
            }
            for (size_t row = 0; row < static_cast<size_t>(l.height); row += rowsPerBlock) {
                if (cancel.load()) {
                    f.close();
                    discard(tempDir);
                    tempCreated = false;
                    return false;
                }
                const size_t rows = std::min(rowsPerBlock, static_cast<size_t>(l.height) - row);
                const auto* start = l.data + row * rowBytes;
                const size_t bytes = rows * rowBytes;
                const QByteArray compressed = qCompress(start, static_cast<int>(bytes), 1);
                QByteArray blockHead;
                putU32(blockHead, static_cast<uint32_t>(bytes));
                putU32(blockHead, static_cast<uint32_t>(compressed.size()));
                putU32(blockHead, core::crc32(start, bytes));
                if (!writeAll(f, blockHead) || !writeAll(f, compressed)) {
                    return fail(QStringLiteral("a level file cannot be written (disk full?)"));
                }
            }
            f.close();
            if (f.error() != QFileDevice::NoError) {
                return fail(QStringLiteral("a level file cannot be written"));
            }
        }
        if (cancel.load()) {
            discard(tempDir);
            tempCreated = false;
            return false;
        }
        if (!writeLastUsed(tempDir)) {
            return fail(QStringLiteral("last-used cannot be written"));
        }

        if (QDir(folder).rename(tempName, QString::fromStdString(name))) {
            return true;
        }
        if (looksValid(finalDir)) {
            // Lost a race to an equivalent entry.
            discard(tempDir);
            tempCreated = false;
            return true;
        }
        return fail(QStringLiteral("the entry cannot be put in place"));
    } catch (const std::exception& e) {
        return fail(QString::fromUtf8(e.what()));
    } catch (...) {
        return fail(QStringLiteral("unknown error"));
    }
}

int BuiltLevelDiskCache::sweep(const std::string& folderUtf8, int retentionDays, int64_t now)
{
    try {
        const QDir root(QString::fromStdString(folderUtf8));
        if (!root.exists()) {
            return 0;
        }
        std::vector<core::CacheEntryFacts> facts;
        const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo& info : dirs) {
            core::CacheEntryFacts f;
            f.name = info.fileName().toStdString();
            f.lastUsedSeconds = info.lastModified().toSecsSinceEpoch();
            QFile lu(info.absoluteFilePath() + QStringLiteral("/last-used"));
            if (lu.open(QIODevice::ReadOnly)) {
                bool ok = false;
                const qlonglong v = lu.readAll().trimmed().toLongLong(&ok);
                if (ok) {
                    f.lastUsedSeconds = v;
                }
            }
            facts.push_back(std::move(f));
        }
        int deleted = 0;
        for (const std::string& name : core::expiredCacheEntries(facts, now, retentionDays)) {
            if (QDir(root.filePath(QString::fromStdString(name))).removeRecursively()) {
                ++deleted;
            }
        }
        return deleted;
    } catch (...) {
        return 0;
    }
}

} // namespace slideio::viewer::infra
