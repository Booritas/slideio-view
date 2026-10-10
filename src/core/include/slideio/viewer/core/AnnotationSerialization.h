#pragma once

#include "slideio/viewer/core/AnnotationDocument.h"

#include <string>
#include <string_view>

namespace slideio::viewer::core
{

/// Renders `document` as pretty-printed JSON with a trailing newline.
///
/// Returns an empty string when the document cannot be represented -- that
/// means a non-finite coordinate, which JSON cannot carry and which nlohmann
/// would otherwise write as `null`, or a timestamp formatIso8601Utc cannot
/// represent (before 1970 or past year 9999), which would be written as an
/// empty string. Either produces a file that will not parse back. Callers must
/// treat an empty result as a failed save rather than writing it out.
std::string serializeAnnotationDocument(const AnnotationDocument& document);

} // namespace slideio::viewer::core
