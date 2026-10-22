#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace slideio::viewer::core
{

/// Where an exported file's annotations came from, written once into the
/// collection's `slideio` member.
struct GeoJsonExportContext
{
    /// The slide as the user opened it, verbatim. Provenance for a reader, not
    /// a path anything reopens.
    std::string slidePath;
    /// core::computeSlideId. May be empty for a slide that could not be
    /// identified; written as it is rather than omitted.
    std::string slideId;
    int sceneIndex = 0;
    /// Pixel size in microns. Zero or less means unknown, and the file then
    /// says nothing about it rather than claiming zero.
    double pixelSizeMicronsX = 0.0;
    double pixelSizeMicronsY = 0.0;
};

struct GeoJsonExportResult
{
    /// The whole file: compact UTF-8 JSON, no byte-order mark, one trailing
    /// newline. Empty on failure.
    std::string text;
    /// Empty on success. On failure, says what could not be written and names
    /// the annotation, so the user can find it.
    std::string error;
};

/// `annotations` as a GeoJSON FeatureCollection that QuPath imports
/// (FR-ANN-34). See docs/superpowers/specs/2026-10-21-annotation-geojson-export-design.md.
///
/// One feature per annotation, in the order given, never filtered: a file
/// that dropped one silently would be read as the whole scene. Every shape is
/// written from core::outline(), the one sequence of points the painter, the
/// hit test and measure() all read, so the file cannot disagree with the
/// screen. Coordinates are level-0 pixels of the scene, exactly as stored.
///
/// Refuses -- empty text, a message in `error` -- rather than writing a value
/// JSON cannot carry: a non-finite coordinate or a timestamp formatIso8601Utc
/// cannot represent. Never write what will not parse back.
///
/// `exportedAt` is a parameter rather than a clock read so this stays pure and
/// a test can assert the field.
[[nodiscard]] GeoJsonExportResult annotationsToGeoJson(
    const std::vector<Annotation>& annotations, const GeoJsonExportContext& context,
    std::chrono::system_clock::time_point exportedAt);

/// What the collection's own `slideio` member said about where the file came
/// from -- present only in a file this application exported.
struct GeoJsonImportProvenance
{
    bool present = false;
    std::string slide;
    std::string slideId;
    /// -1 when the file does not say.
    int scene = -1;
};

/// What a GeoJSON file turned into, and an account of everything it did not.
///
/// Every top-level feature is either imported (one annotation, or one per part
/// of a multi-part geometry) or counted under exactly one skip reason, so
/// `featuresRead == featuresImported + every skip count` and nothing leaves
/// the file unreported.
struct GeoJsonImportResult
{
    /// In file order. An EMPTY id means the caller must mint one: core is
    /// Qt-free and cannot make a UUID.
    std::vector<Annotation> annotations;
    int featuresRead = 0;
    /// Features that produced at least one annotation.
    int featuresImported = 0;
    /// Objects QuPath marks as something other than an annotation --
    /// "detection", "cell", "tile" -- counted under the value it gives.
    std::map<std::string, int> skippedObjectTypes;
    /// A polygon with an interior ring, which the viewer has no shape for.
    int withHoles = 0;
    /// A GeometryCollection, a null geometry, or a type GeoJSON does not define.
    int unsupportedGeometry = 0;
    /// Coordinates that are not finite numbers, or too few of them for the
    /// shape -- the serializer's own minimums, so every annotation returned
    /// is one the application can save.
    int invalidGeometry = 0;
    /// QuPath `childObjects`, counted recursively. Not features of the file's
    /// top level, so outside the accounting identity above.
    int nestedObjects = 0;
    GeoJsonImportProvenance provenance;
    /// Non-empty when the file could not be read at all: then every other
    /// field is empty.
    std::string error;
};

/// The annotations in a GeoJSON file -- one QuPath exports, or one
/// annotationsToGeoJson wrote (FR-ANN-33). See
/// docs/superpowers/specs/2026-10-22-annotation-geojson-import-design.md.
///
/// Coordinates are taken as level-0 pixels of the scene, unchanged. A feature
/// carrying the export's `slideio` block whose type fits its geometry comes
/// back exactly as it was exported -- ellipse, arrow and all; the geometry
/// wins any disagreement. Never throws.
///
/// `importedAt` stamps annotations whose file records no timestamps; it is a
/// parameter so this stays pure.
[[nodiscard]] GeoJsonImportResult geoJsonToAnnotations(
    std::string_view text, std::chrono::system_clock::time_point importedAt);

} // namespace slideio::viewer::core
