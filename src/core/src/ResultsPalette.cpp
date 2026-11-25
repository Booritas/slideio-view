#include "slideio/viewer/core/ResultsPalette.h"

#include "slideio/viewer/core/AnnotationCategories.h"

#include <cstddef>

namespace slideio::viewer::core
{

const std::vector<Color>& resultsPalette()
{
    static const std::vector<Color> palette{
        {0x00, 0x72, 0xB2, 0xFF},  // blue
        {0xE6, 0x7E, 0x22, 0xFF},  // orange
        {0xF0, 0xE4, 0x42, 0xFF},  // yellow
        {0x8E, 0x44, 0xAD, 0xFF},  // purple
        {0x00, 0xBC, 0xD4, 0xFF},  // cyan
        {0x2E, 0xA0, 0x4F, 0xFF},  // green
        {0xD5, 0x2B, 0x1E, 0xFF},  // red
        {0xE7, 0x8A, 0xC3, 0xFF},  // pink
        {0x8C, 0x56, 0x4B, 0xFF},  // brown
        {0x7F, 0x7F, 0x7F, 0xFF},  // grey
        {0xA6, 0xD8, 0x54, 0xFF},  // lime
        {0x1F, 0x3A, 0x93, 0xFF},  // navy
    };
    return palette;
}

Color resultClassColour(const std::string& className, int legendIndex,
                        const std::map<std::string, Color>& overridesByFoldedName)
{
    const auto it = overridesByFoldedName.find(caseFoldKey(className));
    if (it != overridesByFoldedName.end()) {
        return it->second;
    }
    return resultLayerColour(legendIndex);
}

Color resultLayerColour(int layerIndex)
{
    const auto& palette = resultsPalette();
    const auto size = static_cast<int>(palette.size());
    const int i = ((layerIndex % size) + size) % size;
    return palette[static_cast<size_t>(i)];
}

} // namespace slideio::viewer::core
