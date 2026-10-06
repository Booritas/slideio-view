#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace slideio::viewer::core
{

/// Whether a tile holds the scene's pixels as decoded, or converted to sRGB
/// through its ICC profile. Part of the key because the two are different
/// pixel data for the same region: keeping them apart lets both stay cached
/// and stops a read issued before a toggle from being served after it.
enum class ColorMode { Raw, Managed };

class TileKey
{
public:
    TileKey();
    TileKey(int level, int column, int row, int zIndex = 0, int tFrame = 0,
            ColorMode colorMode = ColorMode::Raw);

    int level() const;
    int column() const;
    int row() const;
    int zIndex() const;
    int tFrame() const;
    ColorMode colorMode() const;

    bool operator==(const TileKey& other) const;
    bool operator!=(const TileKey& other) const;

    std::string toString() const;

private:
    int m_level;
    int m_column;
    int m_row;
    int m_zIndex;
    int m_tFrame;
    ColorMode m_colorMode;
};

} // namespace slideio::viewer::core

namespace std
{

template<>
struct hash<slideio::viewer::core::TileKey>
{
    size_t operator()(const slideio::viewer::core::TileKey& key) const noexcept
    {
        size_t seed = hash<int>{}(key.level());
        seed ^= hash<int>{}(key.column()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<int>{}(key.row()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<int>{}(key.zIndex()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<int>{}(key.tFrame()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<int>{}(static_cast<int>(key.colorMode())) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

} // namespace std
