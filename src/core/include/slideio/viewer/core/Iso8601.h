#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace slideio::viewer::core
{

/// Formats as "2026-10-10T14:30:00Z". Sub-second precision is truncated.
/// Returns an empty string for instants before 1970-01-01T00:00:00Z or
/// after 9999-12-31T23:59:59Z. Annotation timestamps are always "now" or a
/// value read back from a file this application wrote, so the floor and
/// ceiling cost nothing.
std::string formatIso8601Utc(std::chrono::system_clock::time_point t);

/// Accepts exactly what formatIso8601Utc produces: 20 characters, zero-padded,
/// 'T' separator, 'Z' suffix, no offsets and no fractional seconds. Rejects
/// invalid calendar dates (e.g. 2026-02-30), leap seconds, and anything with a
/// sign or space in a numeric field. Returns false and leaves `out` untouched
/// on anything else, including all instants before the epoch.
bool parseIso8601Utc(std::string_view text, std::chrono::system_clock::time_point& out);

} // namespace slideio::viewer::core
