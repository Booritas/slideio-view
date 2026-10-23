#pragma once

#include "slideio/viewer/core/AnnotationImport.h"

#include <atomic>
#include <chrono>
#include <string_view>

namespace slideio::viewer::app
{

/// The annotations in an ASAP XML file (FR-ANN-33's other half). See
/// docs/superpowers/specs/2026-10-23-annotation-asap-import-design.md.
///
/// In the app layer rather than core because it reads XML through Qt's
/// QXmlStreamReader: core is Qt-free and has no XML parser, and one importer
/// is no reason to give it a new dependency. App links Qt Core only, so every
/// line is still reachable from app-tests.
///
/// ASAP's types map to the viewer's shapes as the spec's table says; a Spline
/// is imported as a polygon through its control points and counted in
/// `splinesApproximated`, and a type the viewer has no counterpart for is
/// counted in `unsupportedTypes` under its ASAP name. Coordinates are level-0
/// pixels, unchanged; a decimal comma is read as a decimal point.
///
/// ASAP annotations carry no id, and their names are not one -- every file
/// starts at "Annotation 0". Every annotation therefore comes back with an
/// empty id and a content seed in `idSeeds` -- its type, group and coordinates
/// as written -- so the same file imported twice derives the same ids.
///
/// Refuses, with `error` set, anything that is not an ASAP file: XML that does
/// not parse, a root other than ASAP_Annotations, or any document type
/// declaration -- ASAP never writes one, and it is the only way to declare an
/// entity, so refusing it closes both external entities and expansion bombs.
/// Never throws; safe on a worker thread; `cancel` is polled once per token.
[[nodiscard]] core::AnnotationImportResult asapXmlToAnnotations(
    std::string_view text, std::chrono::system_clock::time_point importedAt,
    const std::atomic<bool>* cancel = nullptr);

} // namespace slideio::viewer::app
