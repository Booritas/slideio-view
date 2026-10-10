#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace slideio::viewer::core
{

/// Formats as "2026-10-10T14:30:00Z". Sub-second precision is truncated.
///
/// The representable range starts at 1970-01-01T00:00:00Z. Annotation
/// timestamps are always "now" or a value read back from a file this
/// application wrote, so the floor costs nothing; `parseIso8601Utc` rejects
/// anything below it rather than inventing a value.
std::string formatIso8601Utc(std::chrono::system_clock::time_point t);

/// Accepts exactly what formatIso8601Utc produces: 20 characters, zero-padded,
/// 'T' separator, 'Z' suffix, no offsets and no fractional seconds. Returns
/// false and leaves `out` untouched on anything else, including instants before
/// the epoch.
bool parseIso8601Utc(std::string_view text, std::chrono::system_clock::time_point& out);

} // namespace slideio::viewer::core
