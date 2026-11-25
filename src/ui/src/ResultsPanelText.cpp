#include "slideio/viewer/ui/ResultsPanelText.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace slideio::viewer::ui
{

std::string groupedCount(int64_t n)
{
    // Through the unsigned magnitude so INT64_MIN has one.
    const bool negative = n < 0;
    uint64_t magnitude = negative ? uint64_t{0} - static_cast<uint64_t>(n) : static_cast<uint64_t>(n);
    std::string digits = std::to_string(magnitude);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    const std::size_t lead = digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (i + 3 - lead) % 3 == 0)
            out += ' ';
        out += digits[i];
    }
    return negative ? "-" + out : out;
}

std::string inViewText(int64_t n)
{
    return groupedCount(n) + " in view \xE2\x80\x94 overview; zoom in for outlines";
}

std::string buildingText(double fraction)
{
    int percent = 0;
    if (std::isfinite(fraction)) {
        const double floored = std::floor(fraction * 100.0);
        percent = floored <= 0.0 ? 0 : floored >= 100.0 ? 100 : static_cast<int>(floored);
    }
    return "building overview\xE2\x80\xA6 " + std::to_string(percent) + " %";
}

std::string measurementLine(const core::ResultMeasurement& measurement)
{
    std::ostringstream value;
    value.imbue(std::locale::classic());
    const double v = measurement.value;
    if (v == 0.0 || !std::isfinite(v) || std::fabs(v) >= 1.0)
        value << std::fixed << std::setprecision(1) << v;
    else
        value << std::setprecision(3) << v;

    std::string line = measurement.name + "  " + value.str();
    if (!measurement.unit.empty())
        line += " " + measurement.unit;
    if (!measurement.assertedBy.empty())
        line += " (" + measurement.assertedBy + ")";
    return line;
}

std::string stageStateBadge(const std::string& stageState)
{
    if (stageState == "stale" || stageState == "failed")
        return stageState;
    return {};
}

} // namespace slideio::viewer::ui
