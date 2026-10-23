#pragma once

#include "slideio/viewer/core/AnnotationDocument.h"

#include <string>
#include <string_view>

namespace slideio::viewer::core
{

/// The type name a saved file gives `geometry` -- "rectangle", "ellipse",
/// "polygon", "polyline", "point" or "arrow". Shared with the GeoJSON export so
/// the two files name a shape the same way.
[[nodiscard]] const char* annotationTypeName(const AnnotationGeometry& geometry);

/// `color` as a saved file writes it: `#RRGGBBAA`, upper-case hex.
[[nodiscard]] std::string formatAnnotationColor(const Color& color);

/// Reads `#RRGGBBAA` as formatAnnotationColor writes it into `out`; false, and
/// `out` untouched, for anything else. Shared with the GeoJSON import.
[[nodiscard]] bool parseAnnotationColor(const std::string& text, Color& out);

/// Reads `#RRGGBB` -- ASAP's colours -- into `out` with alpha opaque; false,
/// and `out` untouched, for anything else. Shares parseAnnotationColor's hex
/// reading.
[[nodiscard]] bool parseRgbColor(const std::string& text, Color& out);

/// True when the line width and fill opacity lie inside the ranges a saved
/// file accepts. The GeoJSON import asks before keeping a value from a file,
/// so it cannot build an annotation the serializer would refuse to write.
[[nodiscard]] bool isRepresentableStyle(const AnnotationProperties& properties);

/// Renders `document` as pretty-printed JSON with a trailing newline.
///
/// Returns an empty string when the document cannot be represented -- that
/// means a non-finite coordinate, which JSON cannot carry and which nlohmann
/// would otherwise write as `null`, or a timestamp formatIso8601Utc cannot
/// represent (before 1970 or past year 9999), which would be written as an
/// empty string. Either produces a file that will not parse back. Callers must
/// treat an empty result as a failed save rather than writing it out.
std::string serializeAnnotationDocument(const AnnotationDocument& document);

enum class ParseError
{
    None,
    /// Not JSON at all, or truncated.
    MalformedJson,
    /// Valid JSON, but not this kind of document: no object at the root, or no
    /// numeric schemaVersion.
    NotAnAnnotationDocument,
    /// schemaVersion is higher than this build understands.
    UnsupportedFutureVersion,
    /// The shape is right but a value is not: wrong type, unknown enum,
    /// unparseable timestamp, an out-of-range number. (A coordinate such as 1e400
    /// never gets this far: the JSON lexer rejects it as MalformedJson.)
    InvalidField,
};

struct ParseResult
{
    ParseError error = ParseError::None;
    /// Human-readable, naming the offending field. Shown to the user beside
    /// the file path, so it must say what is wrong, not merely that something is.
    std::string message;
    /// Meaningful only when error == None. On any failure this is a
    /// default-constructed document -- never a partially populated one, which a
    /// caller might otherwise save back over the user's file.
    AnnotationDocument document;
};

/// Parses what serializeAnnotationDocument produces. Never throws: nlohmann's
/// exceptions are caught and turned into a ParseError.
ParseResult parseAnnotationDocument(std::string_view json);

} // namespace slideio::viewer::core
