#pragma once

#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/AnnotationLayer.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// The highest version this build reads. Bumped whenever the on-disk shape
/// changes in a way an older build cannot read correctly. A file carrying a
/// higher number is refused, never partially loaded -- see
/// AnnotationSerialization.h.
///
/// 2 added layers. The WRITTEN version is chosen per file: 2 only when the
/// scene's layers carry information (core::layersCarryInformation), 1
/// otherwise -- so an older build refuses exactly the files whose layer
/// assignments it would otherwise silently drop on its next autosave.
inline constexpr int kCurrentSchemaVersion = 2;

/// The version a file is written as when its layers carry no information:
/// byte for byte the format written before layers existed.
inline constexpr int kLayerlessSchemaVersion = 1;

/// Human-readable provenance, written so a hash-named file can be identified
/// by opening it. Never matched against: the slide identity check uses
/// `AnnotationDocument::slideId`.
struct SlideProvenance
{
    std::string fileName;
    std::string path;
    int64_t width = 0;
    int64_t height = 0;
};

/// One scene's annotations, as stored. The pair (slideId, sceneIndex) is the
/// key: computeSlideId is file-level by construction, so the scene index is
/// what keeps a multi-scene file's scenes apart.
struct AnnotationDocument
{
    int schemaVersion = kCurrentSchemaVersion;
    std::string slideId;
    int sceneIndex = 0;
    SlideProvenance slide;
    std::chrono::system_clock::time_point createdAt{};
    std::chrono::system_clock::time_point modifiedAt{};
    std::vector<Annotation> annotations;
    /// The scene's layers, bottom first. EMPTY when the file said nothing
    /// about layers (every version-1 file): the reader then uses
    /// defaultLayers(). Written only when they carry information.
    std::vector<AnnotationLayer> layers;
};

} // namespace slideio::viewer::core
