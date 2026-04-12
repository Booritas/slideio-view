#include "slideio/viewer/core/TileKey.h"

namespace slideio::viewer::core
{

TileKey::TileKey()
    : m_level(0)
    , m_column(0)
    , m_row(0)
    , m_zIndex(0)
    , m_tFrame(0)
{
}

TileKey::TileKey(int level, int column, int row, int zIndex, int tFrame)
    : m_level(level)
    , m_column(column)
    , m_row(row)
    , m_zIndex(zIndex)
    , m_tFrame(tFrame)
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

int TileKey::zIndex() const
{
    return m_zIndex;
}

int TileKey::tFrame() const
{
    return m_tFrame;
}

bool TileKey::operator==(const TileKey& other) const
{
    return m_level == other.m_level
        && m_column == other.m_column
        && m_row == other.m_row
        && m_zIndex == other.m_zIndex
        && m_tFrame == other.m_tFrame;
}

bool TileKey::operator!=(const TileKey& other) const
{
    return !(*this == other);
}

std::string TileKey::toString() const
{
    std::string s = "TileKey(level=" + std::to_string(m_level)
                  + ", col=" + std::to_string(m_column)
                  + ", row=" + std::to_string(m_row);
    if (m_zIndex != 0 || m_tFrame != 0) {
        s += ", z=" + std::to_string(m_zIndex)
           + ", t=" + std::to_string(m_tFrame);
    }
    s += ")";
    return s;
}

} // namespace slideio::viewer::core
