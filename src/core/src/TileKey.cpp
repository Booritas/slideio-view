#include "slideio/viewer/core/TileKey.h"

namespace slideio::viewer::core
{

TileKey::TileKey()
    : m_level(0)
    , m_column(0)
    , m_row(0)
{
}

TileKey::TileKey(int level, int column, int row)
    : m_level(level)
    , m_column(column)
    , m_row(row)
{
}

int TileKey::level() const
{
    return m_level;
}

int TileKey::column() const
{
    return m_column;
}

int TileKey::row() const
{
    return m_row;
}

bool TileKey::operator==(const TileKey& other) const
{
    return m_level == other.m_level && m_column == other.m_column && m_row == other.m_row;
}

bool TileKey::operator!=(const TileKey& other) const
{
    return !(*this == other);
}

std::string TileKey::toString() const
{
    return "TileKey(level=" + std::to_string(m_level)
         + ", col=" + std::to_string(m_column)
         + ", row=" + std::to_string(m_row) + ")";
}

} // namespace slideio::viewer::core
