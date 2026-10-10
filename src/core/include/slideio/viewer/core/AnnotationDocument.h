#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// Bumped whenever the on-disk shape changes in a way an older build cannot
/// read correctly. A file carrying a higher number is refused, never
/// partially loaded -- see AnnotationSerialization.h.
inline constexpr int kCurrentSchemaVersion = 1;

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
};

} // namespace slideio::viewer::core
