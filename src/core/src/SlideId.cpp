#include "slideio/viewer/core/SlideId.h"

#include <cstdio>
#include <string>

namespace slideio::viewer::core
{

namespace
{

constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

// ASCII unit separator. Cannot occur in any input, all of which are decimal
// digits, so adjacent fields can never run together into a different reading.
constexpr char kFieldSeparator = '\x1F';

void appendField(std::string& buffer, uint64_t value)
{
    buffer += std::to_string(value);
    buffer += kFieldSeparator;
}

void appendField(std::string& buffer, int value)
{
    buffer += std::to_string(value);
    buffer += kFieldSeparator;
}

uint64_t fnv1a64(const std::string& bytes)
{
    uint64_t hash = kFnvOffsetBasis;
    for (const char c : bytes) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

std::string computeSlideId(const std::vector<SceneInfo>& scenes, uint64_t fileSizeBytes)
{
    std::string input;
    input.reserve(32 + scenes.size() * 32);

    appendField(input, fileSizeBytes);
    appendField(input, static_cast<uint64_t>(scenes.size()));

    for (const SceneInfo& scene : scenes) {
        appendField(input, scene.index);
        appendField(input, scene.width);
        appendField(input, scene.height);
        appendField(input, scene.numChannels);
    }

    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(fnv1a64(input)));
    return std::string(hex);
}

std::string computeSlideId(const SlideInfo& info, uint64_t fileSizeBytes)
{
    return computeSlideId(info.scenes, fileSizeBytes);
}

} // namespace slideio::viewer::core
