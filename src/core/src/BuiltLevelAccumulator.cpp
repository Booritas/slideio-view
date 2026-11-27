#include "slideio/viewer/core/BuiltLevelAccumulator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace slideio::viewer::core
{

namespace
{

int ceilDiv(int a, int b)
{
    return (a + b - 1) / b;
}

// Float16 has no standard type and core has no conversion helper; these two
// are private to this file.
float halfToFloat(uint16_t h)
{
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

uint16_t floatToHalf(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const uint32_t exp32 = (bits >> 23) & 0xFFu;
    uint32_t mant = bits & 0x7FFFFFu;
    if (exp32 == 0xFF) {
        return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0u));
    }
    const int e = static_cast<int>(exp32) - 127 + 15;
    if (e >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (e <= 0) {
        if (e < -10) {
            return static_cast<uint16_t>(sign);
        }
        mant |= 0x800000u;
        const int shift = 14 - e;
        uint32_t half = mant >> shift;
        const uint32_t rem = mant & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (half & 1u))) {
            ++half;
        }
        return static_cast<uint16_t>(sign | half);
    }
    uint32_t half = (static_cast<uint32_t>(e) << 10) | (mant >> 13);
    const uint32_t rem = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) {
        ++half; // a carry into the exponent is the correct rounding, up to infinity
    }
    return static_cast<uint16_t>(sign | half);
}

/// How one sample type is read into and written back from a double.
template <typename T>
struct IntCodec
{
    static constexpr size_t kSize = sizeof(T);

    static double load(const uint8_t* p)
    {
        T v;
        std::memcpy(&v, p, sizeof(T));
        return static_cast<double>(v);
    }

    static void store(uint8_t* p, double mean)
    {
        const double lo = static_cast<double>(std::numeric_limits<T>::min());
        const double hi = static_cast<double>(std::numeric_limits<T>::max());
        const double r = std::round(mean); // half away from zero
        T v;
        if (r <= lo) {
            v = std::numeric_limits<T>::min();
        } else if (r >= hi) {
            v = std::numeric_limits<T>::max();
        } else {
            v = static_cast<T>(r);
        }
        std::memcpy(p, &v, sizeof(T));
    }
};

template <typename T>
struct FloatCodec
{
    static constexpr size_t kSize = sizeof(T);

    static double load(const uint8_t* p)
    {
        T v;
        std::memcpy(&v, p, sizeof(T));
        return static_cast<double>(v);
    }

    static void store(uint8_t* p, double mean)
    {
        const T v = static_cast<T>(mean);
        std::memcpy(p, &v, sizeof(T));
    }
};

struct HalfCodec
{
    static constexpr size_t kSize = 2;

    static double load(const uint8_t* p)
    {
        uint16_t h;
        std::memcpy(&h, p, 2);
        return static_cast<double>(halfToFloat(h));
    }

    static void store(uint8_t* p, double mean)
    {
        const uint16_t h = floatToHalf(static_cast<float>(mean));
        std::memcpy(p, &h, 2);
    }
};

/// Calls `f(Codec{})` for the codec of `dt`; returns false for a type with no codec.
template <typename F>
bool withCodec(DataType dt, F&& f)
{
    switch (dt) {
        case DataType::Byte:    f(IntCodec<uint8_t>{}); return true;
        case DataType::Int8:    f(IntCodec<int8_t>{}); return true;
        case DataType::UInt16:  f(IntCodec<uint16_t>{}); return true;
        case DataType::Int16:   f(IntCodec<int16_t>{}); return true;
        case DataType::UInt32:  f(IntCodec<uint32_t>{}); return true;
        case DataType::Int32:   f(IntCodec<int32_t>{}); return true;
        case DataType::Int64:   f(IntCodec<int64_t>{}); return true;
        case DataType::UInt64:  f(IntCodec<uint64_t>{}); return true;
        case DataType::Float16: f(HalfCodec{}); return true;
        case DataType::Float32: f(FloatCodec<float>{}); return true;
        case DataType::Float64: f(FloatCodec<double>{}); return true;
        default:                return false;
    }
}

} // namespace

BuiltLevelAccumulator::BuiltLevelAccumulator(int sourceWidth, int sourceHeight, int numChannels, DataType dataType,
                                             std::vector<int> residentFactors)
    : m_sourceWidth(sourceWidth)
    , m_sourceHeight(sourceHeight)
    , m_numChannels(numChannels)
    , m_dataType(dataType)
{
    const size_t pixelBytes = static_cast<size_t>(numChannels) * dataTypeSize(dataType);
    for (int factor : residentFactors) {
        Level level;
        level.factor = factor;
        level.width = std::max(1, ceilDiv(sourceWidth, factor));
        level.height = std::max(1, ceilDiv(sourceHeight, factor));
        level.data.assign(static_cast<size_t>(level.width) * level.height * pixelBytes, 0);
        level.rowFailed.assign(static_cast<size_t>(level.height), 0);
        m_levels.push_back(std::move(level));
    }
}

int BuiltLevelAccumulator::finestFactor() const
{
    return m_levels.empty() ? 1 : m_levels.front().factor;
}

int BuiltLevelAccumulator::levelWidth(int residentIndex) const
{
    return m_levels.at(static_cast<size_t>(residentIndex)).width;
}

int BuiltLevelAccumulator::levelHeight(int residentIndex) const
{
    return m_levels.at(static_cast<size_t>(residentIndex)).height;
}

bool BuiltLevelAccumulator::complete() const
{
    return !m_levels.empty() && m_levels.back().completedRows >= m_levels.back().height;
}

void BuiltLevelAccumulator::beginBand(int y, int h)
{
    if (m_bandActive) {
        return;
    }
    const Level& finest = m_levels.front();
    m_bandActive = true;
    m_bandY = y;
    m_bandHeight = h;
    m_bandCovered = 0;
    m_bandFailed = false;
    const size_t boxRows = static_cast<size_t>(ceilDiv(h, finest.factor));
    m_bandSum.assign(boxRows * finest.width * m_numChannels, 0.0);
    m_bandCount.assign(boxRows * finest.width, 0);
}

void BuiltLevelAccumulator::noteCoverage(int w)
{
    m_bandCovered += w;
    if (m_bandCovered >= m_sourceWidth) {
        finalizeBand();
    }
}

void BuiltLevelAccumulator::addChunk(int x, int y, int w, int h, const uint8_t* pixels)
{
    if (m_levels.empty() || w <= 0 || h <= 0) {
        return;
    }
    beginBand(y, h);
    const int factor = m_levels.front().factor;
    const int boxesAcross = m_levels.front().width;
    const size_t channels = static_cast<size_t>(m_numChannels);
    const size_t stride = static_cast<size_t>(w) * channels * dataTypeSize(m_dataType);
    const int bandBox0 = m_bandY / factor;

    withCodec(m_dataType, [&](auto codec) {
        using Codec = decltype(codec);
        for (int r = 0; r < h; ++r) {
            const size_t boxRow = static_cast<size_t>((y + r) / factor - bandBox0);
            const uint8_t* src = pixels + static_cast<size_t>(r) * stride;
            for (int i = 0; i < w; ++i) {
                const size_t box = boxRow * boxesAcross + static_cast<size_t>((x + i) / factor);
                double* sum = &m_bandSum[box * channels];
                for (size_t c = 0; c < channels; ++c) {
                    sum[c] += Codec::load(src);
                    src += Codec::kSize;
                }
                ++m_bandCount[box];
            }
        }
    });
    noteCoverage(w);
}

void BuiltLevelAccumulator::failChunk(int /*x*/, int y, int w, int h)
{
    if (m_levels.empty() || w <= 0 || h <= 0) {
        return;
    }
    beginBand(y, h);
    m_bandFailed = true;
    noteCoverage(w);
}

void BuiltLevelAccumulator::finalizeBand()
{
    Level& finest = m_levels.front();
    const int factor = finest.factor;
    const int row0 = m_bandY / factor;
    const int boxRows = std::min(ceilDiv(m_bandHeight, factor), finest.height - row0);
    const size_t channels = static_cast<size_t>(m_numChannels);
    const size_t sampleBytes = dataTypeSize(m_dataType);

    for (int br = 0; br < boxRows; ++br) {
        if (m_bandFailed) {
            finest.rowFailed[static_cast<size_t>(row0 + br)] = 1;
            continue; // the row stays zero
        }
        withCodec(m_dataType, [&](auto codec) {
            using Codec = decltype(codec);
            for (int bx = 0; bx < finest.width; ++bx) {
                const size_t box = static_cast<size_t>(br) * finest.width + bx;
                const uint32_t count = m_bandCount[box];
                uint8_t* dst = finest.data.data()
                    + (static_cast<size_t>(row0 + br) * finest.width + bx) * channels * sampleBytes;
                for (size_t c = 0; c < channels; ++c) {
                    Codec::store(dst + c * sampleBytes, count ? m_bandSum[box * channels + c] / count : 0.0);
                }
            }
        });
    }
    finest.completedRows = std::max(finest.completedRows, row0 + boxRows);
    m_bandActive = false;
    propagate();
}

void BuiltLevelAccumulator::propagate()
{
    const size_t channels = static_cast<size_t>(m_numChannels);
    const size_t sampleBytes = dataTypeSize(m_dataType);
    const size_t pixelBytes = channels * sampleBytes;

    for (size_t i = 1; i < m_levels.size(); ++i) {
        const Level& prev = m_levels[i - 1];
        Level& cur = m_levels[i];
        const int ratio = std::max(1, cur.factor / prev.factor);

        while (cur.completedRows < cur.height) {
            const int j = cur.completedRows;
            const int lo = j * ratio;
            const int hi = std::min(lo + ratio, prev.height);
            if (prev.completedRows < hi) {
                break;
            }
            bool failed = false;
            for (int r = lo; r < hi; ++r) {
                failed = failed || prev.rowFailed[static_cast<size_t>(r)] != 0;
            }
            if (failed) {
                cur.rowFailed[static_cast<size_t>(j)] = 1; // the row stays zero
            } else {
                withCodec(m_dataType, [&](auto codec) {
                    using Codec = decltype(codec);
                    for (int x = 0; x < cur.width; ++x) {
                        const int xlo = x * ratio;
                        const int xhi = std::min(xlo + ratio, prev.width);
                        const double n = static_cast<double>((xhi - xlo) * (hi - lo));
                        uint8_t* dst = cur.data.data() + (static_cast<size_t>(j) * cur.width + x) * pixelBytes;
                        for (size_t c = 0; c < channels; ++c) {
                            double sum = 0.0;
                            for (int r = lo; r < hi; ++r) {
                                for (int xx = xlo; xx < xhi; ++xx) {
                                    sum += Codec::load(prev.data.data()
                                                       + (static_cast<size_t>(r) * prev.width + xx) * pixelBytes
                                                       + c * sampleBytes);
                                }
                            }
                            Codec::store(dst + c * sampleBytes, sum / n);
                        }
                    }
                });
            }
            ++cur.completedRows;
        }
    }
}

BuiltRegionState BuiltLevelAccumulator::regionState(int residentIndex, int y0, int y1) const
{
    const Level& level = m_levels.at(static_cast<size_t>(residentIndex));
    y0 = std::max(0, y0);
    y1 = std::min(level.height, y1);
    if (y0 >= y1) {
        return BuiltRegionState::Ready;
    }
    for (int r = y0; r < y1; ++r) {
        if (level.rowFailed[static_cast<size_t>(r)] != 0) {
            return BuiltRegionState::Failed;
        }
    }
    return y1 <= level.completedRows ? BuiltRegionState::Ready : BuiltRegionState::Pending;
}

TileData BuiltLevelAccumulator::extract(int residentIndex, int x, int y, int w, int h) const
{
    const Level& level = m_levels.at(static_cast<size_t>(residentIndex));
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(level.width, x + w);
    const int y1 = std::min(level.height, y + h);
    const int cw = std::max(0, x1 - x0);
    const int ch = std::max(0, y1 - y0);

    const size_t pixelBytes = static_cast<size_t>(m_numChannels) * dataTypeSize(m_dataType);
    std::vector<uint8_t> out(static_cast<size_t>(cw) * ch * pixelBytes);
    for (int r = 0; r < ch; ++r) {
        const uint8_t* src = level.data.data() + (static_cast<size_t>(y0 + r) * level.width + x0) * pixelBytes;
        std::memcpy(out.data() + static_cast<size_t>(r) * cw * pixelBytes, src, static_cast<size_t>(cw) * pixelBytes);
    }
    return TileData(std::move(out), cw, ch, m_numChannels, m_dataType);
}

} // namespace slideio::viewer::core
