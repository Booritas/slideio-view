#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <chrono>
#include <string>
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

} // namespace slideio::viewer::core
