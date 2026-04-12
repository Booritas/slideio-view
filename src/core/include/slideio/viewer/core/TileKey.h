#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace slideio::viewer::core
{

class TileKey
{
public:
    TileKey();
    TileKey(int level, int column, int row, int zIndex = 0, int tFrame = 0);

    int level() const;
    int column() const;
    int row() const;
    int zIndex() const;
    int tFrame() const;

    bool operator==(const TileKey& other) const;
    bool operator!=(const TileKey& other) const;

    std::string toString() const;

private:
    int m_level;
    int m_column;
    int m_row;
    int m_zIndex;
    int m_tFrame;
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
        return seed;
    }
};

} // namespace std
