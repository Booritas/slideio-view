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
    TileKey(int level, int column, int row);

    int level() const;
    int column() const;
    int row() const;

    bool operator==(const TileKey& other) const;
    bool operator!=(const TileKey& other) const;

    std::string toString() const;

private:
    int m_level;
    int m_column;
    int m_row;
};

} // namespace slideio::viewer::core

namespace std
{

template<>
struct hash<slideio::viewer::core::TileKey>
{
    size_t operator()(const slideio::viewer::core::TileKey& key) const noexcept
    {
        size_t h1 = hash<int>{}(key.level());
        size_t h2 = hash<int>{}(key.column());
        size_t h3 = hash<int>{}(key.row());
        // Combine hashes using a standard mixing technique
        size_t seed = h1;
        seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

} // namespace std
