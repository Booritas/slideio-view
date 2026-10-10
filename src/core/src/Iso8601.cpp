#include "slideio/viewer/core/Iso8601.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace slideio::viewer::core
{

namespace
{

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

/// Reads exactly `count` decimal digits at `offset`. Rejects anything else --
/// no sign, no space, no short field. This is what keeps the parser to exactly
/// the format the writer emits.
bool readFixedDigits(std::string_view text, std::size_t offset, std::size_t count, int& out)
{
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[offset + i];
        if (!isDigit(c)) {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month)
{
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

/// Days since 1970-01-01 for a proleptic Gregorian date. Howard Hinnant's
/// days_from_civil. Used instead of timegm/_mkgmtime because those two disagree
/// about dates before the epoch and both silently normalise invalid ones.
int64_t daysFromCivil(int64_t year, int64_t month, int64_t day)
{
    year -= (month <= 2) ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yoe = year - era * 400;                                    // [0, 399]
    const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;               // [0, 146096]
    return era * 146097 + doe - 719468;
}

/// The inverse of daysFromCivil.
void civilFromDays(int64_t z, int& year, int& month, int& day)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;                                    // [0, 146096]
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);             // [0, 365]
    const int64_t mp = (5 * doy + 2) / 153;                                  // [0, 11]
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;                          // [1, 31]
    const int64_t m = mp + (mp < 10 ? 3 : -9);                               // [1, 12]
    year = static_cast<int>(y + (m <= 2 ? 1 : 0));
    month = static_cast<int>(m);
    day = static_cast<int>(d);
}

constexpr int64_t kSecondsPerDay = 86400;
constexpr int kMaxYear = 9999;

} // namespace

std::string formatIso8601Utc(std::chrono::system_clock::time_point t)
{
    const auto truncated = std::chrono::time_point_cast<std::chrono::seconds>(t);
    const int64_t total = static_cast<int64_t>(std::chrono::system_clock::to_time_t(truncated));

    // Outside the representable range there is no honest 20-character answer,
    // and emitting a wrong one that looks valid is worse than emitting none.
    if (total < 0) {
        return {};
    }

    const int64_t days = total / kSecondsPerDay;
    const int64_t secondOfDay = total % kSecondsPerDay;

    int year = 0;
    int month = 0;
    int day = 0;
    civilFromDays(days, year, month, day);
    if (year > kMaxYear) {
        return {};
    }

    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(),
                                      "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                      year, month, day,
                                      static_cast<int>(secondOfDay / 3600),
                                      static_cast<int>((secondOfDay / 60) % 60),
                                      static_cast<int>(secondOfDay % 60));
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
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T'
        || text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!readFixedDigits(text, 0, 4, year) || !readFixedDigits(text, 5, 2, month)
        || !readFixedDigits(text, 8, 2, day) || !readFixedDigits(text, 11, 2, hour)
        || !readFixedDigits(text, 14, 2, minute) || !readFixedDigits(text, 17, 2, second)) {
        return false;
    }

    if (month < 1 || month > 12) {
        return false;
    }
    // Validated against the actual month, so 2026-02-30 is rejected rather than
    // rolled forward into March.
    if (day < 1 || day > daysInMonth(year, month)) {
        return false;
    }
    // 60 would be a leap second. The writer never emits one, and accepting it
    // would roll into the next minute.
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }

    const int64_t total = daysFromCivil(year, month, day) * kSecondsPerDay
                          + hour * 3600 + minute * 60 + second;
    if (total < 0) {
        return false;
    }

    out = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(total));
    return true;
}

} // namespace slideio::viewer::core
