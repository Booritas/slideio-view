#include "slideio/viewer/core/Iso8601.h"

#include <array>
#include <cstdio>
#include <ctime>

namespace slideio::viewer::core
{

namespace
{

std::tm toUtcTm(std::time_t seconds)
{
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    return tm;
}

/// -1 doubles as "out of range" here. It is also a legitimate instant
/// (1969-12-31T23:59:59Z), but the format's floor is the epoch, so treating it
/// as failure is correct rather than merely convenient.
std::time_t fromUtcTm(std::tm& tm)
{
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

} // namespace

std::string formatIso8601Utc(std::chrono::system_clock::time_point t)
{
    const auto truncated = std::chrono::time_point_cast<std::chrono::seconds>(t);
    const std::time_t seconds = std::chrono::system_clock::to_time_t(truncated);
    const std::tm tm = toUtcTm(seconds);

    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(),
                                      "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                      tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (written <= 0) {
        return {};
    }
    return std::string(buffer.data(), static_cast<std::size_t>(written));
}

bool parseIso8601Utc(std::string_view text, std::chrono::system_clock::time_point& out)
{
    if (text.size() != 20) {
        return false;
    }

    // sscanf needs a NUL-terminated buffer, and string_view does not promise one.
    const std::string copy(text);
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    char suffix = '\0';
    const int fields = std::sscanf(copy.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c",
                                   &year, &month, &day, &hour, &minute, &second, &suffix);
    if (fields != 7 || suffix != 'Z') {
        return false;
    }

    // sscanf's %2d happily accepts "3-" where the format demands "03", so the
    // separators are checked by position rather than trusted to the scan.
    if (copy[4] != '-' || copy[7] != '-' || copy[10] != 'T' || copy[13] != ':' || copy[16] != ':') {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return false;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;

    const std::time_t seconds = fromUtcTm(tm);
    if (seconds == static_cast<std::time_t>(-1)) {
        return false;
    }
    out = std::chrono::system_clock::from_time_t(seconds);
    return true;
}

} // namespace slideio::viewer::core
