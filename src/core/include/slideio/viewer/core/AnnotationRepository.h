#pragma once

#include "slideio/viewer/core/AnnotationDocument.h"

#include <string>

namespace slideio::viewer::core
{

/// What identifies one annotation file.
///
/// The scene index is not decoration. computeSlideId is file-level by
/// construction -- its own documentation says it is "identical whichever scene
/// of a multi-scene file is open" -- while annotations live in the coordinate
/// space of one scene. Keyed on the slide id alone, a multi-scene file's scene
/// 1 would load scene 0's annotations at scene 0's coordinates.
struct AnnotationKey
{
    std::string slideId;
    int sceneIndex = 0;
};

/// Equality is over both fields. The scene index is half the key: a file whose
/// content names a different scene than the one it was opened for belongs to a
/// different coordinate space.
inline bool operator==(const AnnotationKey& lhs, const AnnotationKey& rhs)
{
    return lhs.slideId == rhs.slideId && lhs.sceneIndex == rhs.sceneIndex;
}

inline bool operator!=(const AnnotationKey& lhs, const AnnotationKey& rhs)
{
    return !(lhs == rhs);
}

enum class LoadStatus
{
    Loaded,
    /// No file for this key. The normal state of a slide nobody has annotated.
    NotFound,
    /// The file exists but could not be read: permissions, I/O error.
    Unreadable,
    /// The file was read but could not be parsed.
    Malformed,
    /// Written by a newer version of the application.
    UnsupportedVersion,
};

struct LoadResult
{
    LoadStatus status = LoadStatus::NotFound;
    std::string message;
    std::string path;
    AnnotationDocument document;
};

enum class SaveStatus
{
    Saved,
    /// The workspace directory is missing, or not writable.
    NotWritable,
    /// Everything else: a serialization refusal, a failed rename, a full disk.
    Failed,
};

struct SaveResult
{
    SaveStatus status = SaveStatus::Failed;
    std::string message;
    std::string path;
};

/// Reads and writes annotation documents. Implemented by
/// infra::JsonAnnotationRepository.
///
/// Stateless with respect to past calls: it cannot know that a previous load
/// failed, so the rule that "a file we could not parse is never a file we write
/// to" is enforced one layer up, in AnnotationPersistenceService.
class IAnnotationRepository
{
public:
    virtual ~IAnnotationRepository() = default;

    IAnnotationRepository(const IAnnotationRepository&) = delete;
    IAnnotationRepository& operator=(const IAnnotationRepository&) = delete;
    IAnnotationRepository(IAnnotationRepository&&) = delete;
    IAnnotationRepository& operator=(IAnnotationRepository&&) = delete;

    virtual LoadResult load(const AnnotationKey& key) = 0;
    virtual SaveResult save(const AnnotationDocument& document) = 0;

    /// Where `key` lives, whether or not anything is there. Exists so an error
    /// message can name a real path without the caller rebuilding the naming rule.
    [[nodiscard]] virtual std::string pathFor(const AnnotationKey& key) const = 0;

protected:
    IAnnotationRepository() = default;
};

} // namespace slideio::viewer::core
