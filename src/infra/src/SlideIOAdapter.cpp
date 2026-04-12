#include "slideio/viewer/infra/SlideIOAdapter.h"

#include <slideio/slideio/slideio.hpp>
#include <slideio/slideio/slide.hpp>
#include <slideio/slideio/scene.hpp>
#include <slideio/core/levelinfo.hpp>
#include <slideio/base/slideio_enums.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace
{

slideio::viewer::core::DataType convertSlideIODataType(::slideio::DataType srcType)
{
    using DT = slideio::viewer::core::DataType;
    switch (srcType) {
        case ::slideio::DataType::DT_Byte:    return DT::Byte;
        case ::slideio::DataType::DT_Int8:     return DT::Int8;
        case ::slideio::DataType::DT_UInt16:   return DT::UInt16;
        case ::slideio::DataType::DT_Int16:    return DT::Int16;
        case ::slideio::DataType::DT_UInt32:   return DT::UInt32;
        case ::slideio::DataType::DT_Int32:    return DT::Int32;
        case ::slideio::DataType::DT_Float16:  return DT::Float16;
        case ::slideio::DataType::DT_Float32:  return DT::Float32;
        case ::slideio::DataType::DT_Float64:  return DT::Float64;
        case ::slideio::DataType::DT_Int64:    return DT::Int64;
        case ::slideio::DataType::DT_UInt64:   return DT::UInt64;
        case ::slideio::DataType::DT_Unknown:  return DT::Unknown;
        case ::slideio::DataType::DT_None:     return DT::None;
        default:                               return DT::Unknown;
    }
}

constexpr int kDefaultTileSize = 256;

} // anonymous namespace

namespace slideio::viewer::infra
{

SlideIOAdapter::SlideIOAdapter(const std::string& filePath, int sceneIndex)
    : m_filePath(filePath)
    , m_sceneIndex(sceneIndex)
{
    spdlog::info("SlideIOAdapter: opening slide '{}', scene {}", filePath, sceneIndex);

    m_slide = ::slideio::openSlide(filePath);
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    int numScenes = m_slide->getNumScenes();
    if (numScenes <= 0) {
        throw std::runtime_error("SlideIOAdapter: slide '" + filePath + "' has no scenes");
    }

    if (sceneIndex < 0 || sceneIndex >= numScenes) {
        throw std::runtime_error("SlideIOAdapter: scene index " + std::to_string(sceneIndex)
            + " out of range [0, " + std::to_string(numScenes) + ") for slide '" + filePath + "'");
    }

    m_scene = m_slide->getScene(sceneIndex);
    if (!m_scene) {
        throw std::runtime_error("SlideIOAdapter: failed to get scene " + std::to_string(sceneIndex)
            + " from slide '" + filePath + "'");
    }

    // Build cached slide info
    auto rect = m_scene->getRect();
    m_slideInfo.filePath = filePath;
    m_slideInfo.width = std::get<2>(rect);
    m_slideInfo.height = std::get<3>(rect);
    m_slideInfo.numChannels = m_scene->getNumChannels();
    m_slideInfo.channelDataType = (m_slideInfo.numChannels > 0)
        ? convertSlideIODataType(m_scene->getChannelDataType(0))
        : core::DataType::None;
    m_slideInfo.magnification = m_scene->getMagnification();

    auto resolution = m_scene->getResolution();
    m_slideInfo.resolutionX = std::get<0>(resolution);
    m_slideInfo.resolutionY = std::get<1>(resolution);

    m_slideInfo.driverName = m_slide->getDriverId();

    // Read per-channel info (names, data types, default colors)
    m_slideInfo.channels.resize(static_cast<size_t>(m_slideInfo.numChannels));
    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        try {
            info.name = m_scene->getChannelName(ch);
        } catch (...) {
            info.name = "Channel " + std::to_string(ch);
        }
        info.dataType = convertSlideIODataType(m_scene->getChannelDataType(ch));
        info.visible = true;
        core::assignDefaultFluorescenceColor(info, ch);
    }

    // Determine if this is a brightfield slide
    m_slideInfo.isBrightfield =
        (m_slideInfo.numChannels == 1) ||
        (m_slideInfo.numChannels == 3 && m_slideInfo.channelDataType == core::DataType::Byte);

    spdlog::info("SlideIOAdapter: isBrightfield={}, {} channels", m_slideInfo.isBrightfield, m_slideInfo.numChannels);
    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        const auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        spdlog::info("  channel {}: name='{}' color=({:.2f},{:.2f},{:.2f})",
                     ch, info.name, info.colorR, info.colorG, info.colorB);
    }

    // Build level info
    int numZoomLevels = m_scene->getNumZoomLevels();
    m_slideInfo.numZoomLevels = numZoomLevels;

    if (numZoomLevels > 0) {
        for (int i = 0; i < numZoomLevels; ++i) {
            const ::slideio::LevelInfo* srcLevel = m_scene->getLevelInfo(i);
            if (!srcLevel) {
                spdlog::warn("SlideIOAdapter: getLevelInfo({}) returned null, skipping", i);
                continue;
            }

            core::LevelInfo lvl;
            lvl.level = srcLevel->getLevel();

            auto levelSize = srcLevel->getSize();
            lvl.width = levelSize.width;
            lvl.height = levelSize.height;
            // Normalize scale to the project's convention:
            //   level.scale = level_pixels / slide_pixels
            // so level 0 is ~1.0 and coarser levels are <1.0.
            // Some backends report the inverse (downsample factor > 1), so we
            // derive from level dimensions first and only fall back to metadata.
            double scaleFromMetadata = srcLevel->getScale();
            double scaleFromSize = 0.0;
            if (m_slideInfo.width > 0 && m_slideInfo.height > 0 && lvl.width > 0 && lvl.height > 0) {
                double sx = static_cast<double>(lvl.width) / static_cast<double>(m_slideInfo.width);
                double sy = static_cast<double>(lvl.height) / static_cast<double>(m_slideInfo.height);
                scaleFromSize = std::min(sx, sy);
            }

            if (scaleFromSize > 0.0) {
                lvl.scale = scaleFromSize;
            } else if (scaleFromMetadata > 1.0) {
                lvl.scale = 1.0 / scaleFromMetadata;
            } else {
                lvl.scale = scaleFromMetadata;
            }

            if (lvl.scale <= 0.0) {
                lvl.scale = 1.0;
            }
            lvl.magnification = srcLevel->getMagnification();

            auto tileSize = srcLevel->getTileSize();
            lvl.tileWidth = tileSize.width;
            lvl.tileHeight = tileSize.height;

            // If tile size is zero or negative, use default
            if (lvl.tileWidth <= 0 || lvl.tileHeight <= 0) {
                lvl.tileWidth = std::min(kDefaultTileSize, lvl.width);
                lvl.tileHeight = std::min(kDefaultTileSize, lvl.height);
            }

            // Compute tile counts
            lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
                ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
                : 1;
            lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
                ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
                : 1;

            m_levels.push_back(lvl);
        }
    }

    // Fallback: create a synthetic single level from full dimensions
    if (m_levels.empty()) {
        spdlog::info("SlideIOAdapter: scene has 0 zoom levels, creating synthetic single level");

        core::LevelInfo lvl;
        lvl.level = 0;
        lvl.width = m_slideInfo.width;
        lvl.height = m_slideInfo.height;
        lvl.scale = 1.0;
        lvl.magnification = m_slideInfo.magnification;
        lvl.tileWidth = std::min(kDefaultTileSize, m_slideInfo.width);
        lvl.tileHeight = std::min(kDefaultTileSize, m_slideInfo.height);
        lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
            ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
            : 1;
        lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
            ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
            : 1;

        m_levels.push_back(lvl);
        m_slideInfo.numZoomLevels = 1;
    }

    spdlog::info("SlideIOAdapter: opened slide {}x{}, {} channels, {} levels",
                 m_slideInfo.width, m_slideInfo.height, m_slideInfo.numChannels,
                 static_cast<int>(m_levels.size()));
}

SlideIOAdapter::SlideIOAdapter(const std::string& filePath, const std::string& auxImageName)
    : m_filePath(filePath)
    , m_sceneIndex(-1)
{
    spdlog::info("SlideIOAdapter: opening slide '{}', aux image '{}'", filePath, auxImageName);

    m_slide = ::slideio::openSlide(filePath);
    if (!m_slide) {
        throw std::runtime_error("SlideIOAdapter: failed to open slide '" + filePath + "'");
    }

    m_scene = m_slide->getAuxImage(auxImageName);
    if (!m_scene) {
        throw std::runtime_error("SlideIOAdapter: failed to get aux image '" + auxImageName
            + "' from slide '" + filePath + "'");
    }

    // Build cached slide info
    auto rect = m_scene->getRect();
    m_slideInfo.filePath = filePath;
    m_slideInfo.width = std::get<2>(rect);
    m_slideInfo.height = std::get<3>(rect);
    m_slideInfo.numChannels = m_scene->getNumChannels();
    m_slideInfo.channelDataType = (m_slideInfo.numChannels > 0)
        ? convertSlideIODataType(m_scene->getChannelDataType(0))
        : core::DataType::None;
    m_slideInfo.magnification = m_scene->getMagnification();

    auto resolution = m_scene->getResolution();
    m_slideInfo.resolutionX = std::get<0>(resolution);
    m_slideInfo.resolutionY = std::get<1>(resolution);

    m_slideInfo.driverName = m_slide->getDriverId();

    // Read per-channel info (names, data types, default colors)
    m_slideInfo.channels.resize(static_cast<size_t>(m_slideInfo.numChannels));
    for (int ch = 0; ch < m_slideInfo.numChannels; ++ch) {
        auto& info = m_slideInfo.channels[static_cast<size_t>(ch)];
        try {
            info.name = m_scene->getChannelName(ch);
        } catch (...) {
            info.name = "Channel " + std::to_string(ch);
        }
        info.dataType = convertSlideIODataType(m_scene->getChannelDataType(ch));
        info.visible = true;
        core::assignDefaultFluorescenceColor(info, ch);
    }

    // Determine if this is a brightfield slide
    m_slideInfo.isBrightfield =
        (m_slideInfo.numChannels == 1) ||
        (m_slideInfo.numChannels == 3 && m_slideInfo.channelDataType == core::DataType::Byte);

    spdlog::info("SlideIOAdapter: isBrightfield={}, {} channels", m_slideInfo.isBrightfield, m_slideInfo.numChannels);

    // Build level info
    int numZoomLevels = m_scene->getNumZoomLevels();
    m_slideInfo.numZoomLevels = numZoomLevels;

    if (numZoomLevels > 0) {
        for (int i = 0; i < numZoomLevels; ++i) {
            const ::slideio::LevelInfo* srcLevel = m_scene->getLevelInfo(i);
            if (!srcLevel) {
                spdlog::warn("SlideIOAdapter: getLevelInfo({}) returned null, skipping", i);
                continue;
            }

            core::LevelInfo lvl;
            lvl.level = srcLevel->getLevel();

            auto levelSize = srcLevel->getSize();
            lvl.width = levelSize.width;
            lvl.height = levelSize.height;

            double scaleFromMetadata = srcLevel->getScale();
            double scaleFromSize = 0.0;
            if (m_slideInfo.width > 0 && m_slideInfo.height > 0 && lvl.width > 0 && lvl.height > 0) {
                double sx = static_cast<double>(lvl.width) / static_cast<double>(m_slideInfo.width);
                double sy = static_cast<double>(lvl.height) / static_cast<double>(m_slideInfo.height);
                scaleFromSize = std::min(sx, sy);
            }

            if (scaleFromSize > 0.0) {
                lvl.scale = scaleFromSize;
            } else if (scaleFromMetadata > 1.0) {
                lvl.scale = 1.0 / scaleFromMetadata;
            } else {
                lvl.scale = scaleFromMetadata;
            }

            if (lvl.scale <= 0.0) {
                lvl.scale = 1.0;
            }
            lvl.magnification = srcLevel->getMagnification();

            auto tileSize = srcLevel->getTileSize();
            lvl.tileWidth = tileSize.width;
            lvl.tileHeight = tileSize.height;

            if (lvl.tileWidth <= 0 || lvl.tileHeight <= 0) {
                lvl.tileWidth = std::min(kDefaultTileSize, lvl.width);
                lvl.tileHeight = std::min(kDefaultTileSize, lvl.height);
            }

            lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
                ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
                : 1;
            lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
                ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
                : 1;

            m_levels.push_back(lvl);
        }
    }

    // Fallback: create a synthetic single level from full dimensions
    if (m_levels.empty()) {
        spdlog::info("SlideIOAdapter: aux image has 0 zoom levels, creating synthetic single level");

        core::LevelInfo lvl;
        lvl.level = 0;
        lvl.width = m_slideInfo.width;
        lvl.height = m_slideInfo.height;
        lvl.scale = 1.0;
        lvl.magnification = m_slideInfo.magnification;
        lvl.tileWidth = std::min(kDefaultTileSize, m_slideInfo.width);
        lvl.tileHeight = std::min(kDefaultTileSize, m_slideInfo.height);
        lvl.tilesX = (lvl.width > 0 && lvl.tileWidth > 0)
            ? (lvl.width + lvl.tileWidth - 1) / lvl.tileWidth
            : 1;
        lvl.tilesY = (lvl.height > 0 && lvl.tileHeight > 0)
            ? (lvl.height + lvl.tileHeight - 1) / lvl.tileHeight
            : 1;

        m_levels.push_back(lvl);
        m_slideInfo.numZoomLevels = 1;
    }

    spdlog::info("SlideIOAdapter: opened aux image '{}' {}x{}, {} channels, {} levels",
                 auxImageName, m_slideInfo.width, m_slideInfo.height, m_slideInfo.numChannels,
                 static_cast<int>(m_levels.size()));
}

std::pair<std::vector<core::SceneInfo>, std::vector<core::SceneInfo>> SlideIOAdapter::enumerateScenes(
    const std::string& filePath)
{
    std::vector<core::SceneInfo> scenes;
    std::vector<core::SceneInfo> auxImages;

    try {
        auto slide = ::slideio::openSlide(filePath);
        if (!slide) {
            spdlog::error("SlideIOAdapter::enumerateScenes: failed to open slide '{}'", filePath);
            return {scenes, auxImages};
        }

        // Enumerate main scenes
        int numScenes = slide->getNumScenes();
        for (int i = 0; i < numScenes; ++i) {
            try {
                auto scene = slide->getScene(i);
                if (!scene) {
                    continue;
                }

                core::SceneInfo info;
                info.index = i;
                info.name = scene->getName();
                auto rect = scene->getRect();
                info.width = std::get<2>(rect);
                info.height = std::get<3>(rect);
                info.numChannels = scene->getNumChannels();
                info.isAuxiliary = false;

                scenes.push_back(std::move(info));
            } catch (const std::exception& ex) {
                spdlog::warn("SlideIOAdapter::enumerateScenes: failed to read scene {}: {}", i, ex.what());
            }
        }

        // Enumerate auxiliary images
        auto auxNames = slide->getAuxImageNames();
        for (const auto& auxName : auxNames) {
            try {
                auto auxScene = slide->getAuxImage(auxName);
                if (!auxScene) {
                    continue;
                }

                core::SceneInfo info;
                info.index = -1;
                info.name = auxScene->getName();
                auto rect = auxScene->getRect();
                info.width = std::get<2>(rect);
                info.height = std::get<3>(rect);
                info.numChannels = auxScene->getNumChannels();
                info.isAuxiliary = true;
                info.auxiliaryName = auxName;

                auxImages.push_back(std::move(info));
            } catch (const std::exception& ex) {
                spdlog::warn("SlideIOAdapter::enumerateScenes: failed to read aux image '{}': {}", auxName, ex.what());
            }
        }
    } catch (const std::exception& ex) {
        spdlog::error("SlideIOAdapter::enumerateScenes: exception opening slide '{}': {}", filePath, ex.what());
    }

    spdlog::info("SlideIOAdapter::enumerateScenes: '{}' has {} scenes, {} aux images",
                 filePath, scenes.size(), auxImages.size());

    return {scenes, auxImages};
}

SlideIOAdapter::~SlideIOAdapter()
{
    spdlog::debug("SlideIOAdapter: closing slide '{}'", m_filePath);
}

core::SlideInfo SlideIOAdapter::slideInfo() const
{
    return m_slideInfo;
}

std::vector<core::LevelInfo> SlideIOAdapter::levels() const
{
    return m_levels;
}

core::TileData SlideIOAdapter::readTile(const core::TileKey& key)
{
    int level = key.level();
    int col = key.column();
    int row = key.row();

    if (level < 0 || level >= static_cast<int>(m_levels.size())) {
        spdlog::error("SlideIOAdapter::readTile: level {} out of range [0, {})", level, m_levels.size());
        return core::TileData::createError(0, 0);
    }

    const auto& lvl = m_levels[static_cast<size_t>(level)];

    if (col < 0 || col >= lvl.tilesX || row < 0 || row >= lvl.tilesY) {
        spdlog::error("SlideIOAdapter::readTile: tile ({},{}) out of range for level {} ({}x{} tiles)",
                      col, row, level, lvl.tilesX, lvl.tilesY);
        return core::TileData::createError(lvl.tileWidth, lvl.tileHeight);
    }

    try {
        // Compute the pixel rectangle in level coordinates
        int tileX = col * lvl.tileWidth;
        int tileY = row * lvl.tileHeight;
        int tileW = std::min(lvl.tileWidth, lvl.width - tileX);
        int tileH = std::min(lvl.tileHeight, lvl.height - tileY);

        if (tileW <= 0 || tileH <= 0) {
            spdlog::error("SlideIOAdapter::readTile: computed tile dimensions {}x{} invalid", tileW, tileH);
            return core::TileData::createError(lvl.tileWidth, lvl.tileHeight);
        }

        // Convert level-coordinate rect to slide-coordinate rect.
        // lvl.scale = level_pixels / slide_pixels, so slide_coord = level_coord / scale.
        double invScale = (lvl.scale > 0.0) ? (1.0 / lvl.scale) : 1.0;
        int slideX = static_cast<int>(std::round(tileX * invScale));
        int slideY = static_cast<int>(std::round(tileY * invScale));
        int slideRight = static_cast<int>(std::round((tileX + tileW) * invScale));
        int slideBottom = static_cast<int>(std::round((tileY + tileH) * invScale));
        int slideW = slideRight - slideX;
        int slideH = slideBottom - slideY;

        // Clamp to slide bounds
        slideW = std::min(slideW, m_slideInfo.width - slideX);
        slideH = std::min(slideH, m_slideInfo.height - slideY);

        if (slideW <= 0 || slideH <= 0) {
            spdlog::error("SlideIOAdapter::readTile: slide rect {}x{} invalid after clamping", slideW, slideH);
            return core::TileData::createError(tileW, tileH);
        }

        std::tuple<int, int, int, int> blockRect(slideX, slideY, slideW, slideH);
        std::tuple<int, int> blockSize(tileW, tileH);

        // Build channel index vector for all channels
        int numChannels = m_slideInfo.numChannels;
        std::vector<int> channels(static_cast<size_t>(numChannels));
        for (int ch = 0; ch < numChannels; ++ch) {
            channels[static_cast<size_t>(ch)] = ch;
        }

        size_t pixelBytes = core::dataTypeSize(m_slideInfo.channelDataType);
        size_t bufSize = static_cast<size_t>(tileW) * static_cast<size_t>(tileH)
                         * static_cast<size_t>(numChannels) * pixelBytes;

        std::vector<uint8_t> buffer(bufSize);

        m_scene->readResampledBlockChannels(blockRect, blockSize, channels, buffer.data(), bufSize);

        return core::TileData(std::move(buffer), tileW, tileH, numChannels, m_slideInfo.channelDataType);
    }
    catch (const std::exception& ex) {
        spdlog::error("SlideIOAdapter::readTile: exception reading tile {}: {}", key.toString(), ex.what());
        return core::TileData::createError(lvl.tileWidth, lvl.tileHeight);
    }
}

} // namespace slideio::viewer::infra
