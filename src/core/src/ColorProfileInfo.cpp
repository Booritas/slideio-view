#include "slideio/viewer/core/Types.h"

#include <cctype>

namespace slideio::viewer::core
{

namespace
{

// The parenthetical in the summary line reads as prose, so it is the display
// name with a lowercase initial rather than a second table to keep in step.
std::string lowerInitial(const char* name)
{
    std::string text(name);
    if (!text.empty()) {
        text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    }
    return text;
}

} // namespace

const char* colorProfileSourceName(ColorProfileSource source)
{
    switch (source) {
    case ColorProfileSource::None:     return "None";
    case ColorProfileSource::Embedded: return "Embedded";
    case ColorProfileSource::Assumed:  return "Assumed";
    case ColorProfileSource::Supplied: return "Supplied";
    }
    return "Unknown";
}

const char* iccColorSpaceName(IccColorSpace space)
{
    switch (space) {
    case IccColorSpace::Unknown: return "Unknown";
    case IccColorSpace::Gray:    return "Gray";
    case IccColorSpace::RGB:     return "RGB";
    case IccColorSpace::CMYK:    return "CMYK";
    case IccColorSpace::Lab:     return "Lab";
    case IccColorSpace::XYZ:     return "XYZ";
    case IccColorSpace::YCbCr:   return "YCbCr";
    }
    return "Unknown";
}

const char* renderingIntentName(RenderingIntent intent)
{
    switch (intent) {
    case RenderingIntent::Perceptual:           return "Perceptual";
    case RenderingIntent::RelativeColorimetric: return "Relative colorimetric";
    case RenderingIntent::Saturation:           return "Saturation";
    case RenderingIntent::AbsoluteColorimetric: return "Absolute colorimetric";
    }
    return "Unknown";
}

std::string colorProfileSummary(const ColorProfileInfo& info)
{
    if (!info.present) {
        return "None (not color-managed)";
    }

    // The desc tag is optional in practice, and a bare "(embedded)" tells a
    // reader nothing. Fall back to the device model, then to a generic name.
    std::string name = info.description;
    if (name.empty()) {
        name = info.model;
    }
    if (name.empty()) {
        name = "ICC profile";
    }

    return name + " (" + lowerInitial(colorProfileSourceName(info.source)) + ")";
}

} // namespace slideio::viewer::core
