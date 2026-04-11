#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <vector>

namespace slideio::viewer::core
{

class TileData
{
public:
    TileData()
        : m_width(0)
        , m_height(0)
        , m_numChannels(0)
        , m_dataType(DataType::None)
        , m_isError(false)
    {
    }

    TileData(std::vector<uint8_t> buffer, int width, int height, int numChannels, DataType dataType)
        : m_buffer(std::move(buffer))
        , m_width(width)
        , m_height(height)
        , m_numChannels(numChannels)
        , m_dataType(dataType)
        , m_isError(false)
    {
    }

    const std::vector<uint8_t>& buffer() const { return m_buffer; }
    std::vector<uint8_t>& buffer() { return m_buffer; }

    int width() const { return m_width; }
    int height() const { return m_height; }
    int numChannels() const { return m_numChannels; }
    DataType dataType() const { return m_dataType; }
    bool isError() const { return m_isError; }

    void setWidth(int w) { m_width = w; }
    void setHeight(int h) { m_height = h; }
    void setNumChannels(int c) { m_numChannels = c; }
    void setDataType(DataType dt) { m_dataType = dt; }
    void setIsError(bool err) { m_isError = err; }

    size_t byteSize() const
    {
        return m_buffer.size();
    }

    bool isEmpty() const
    {
        return m_buffer.empty();
    }

    static TileData createError(int w, int h)
    {
        TileData tile;
        tile.m_width = w;
        tile.m_height = h;
        tile.m_numChannels = 0;
        tile.m_dataType = DataType::None;
        tile.m_isError = true;
        return tile;
    }

private:
    std::vector<uint8_t> m_buffer;
    int m_width;
    int m_height;
    int m_numChannels;
    DataType m_dataType;
    bool m_isError;
};

} // namespace slideio::viewer::core
