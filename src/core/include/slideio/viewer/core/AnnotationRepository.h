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
    /// Nothing was attempted: no slide, nothing to write, or writing disabled.
    /// A repository never returns this; AnnotationPersistenceService does.
    NothingToDo,
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

enum class LockStatus
{
    /// This process now holds it, and must release it.
    Acquired,
    /// Another process holds it. Its annotations may be shown; none may be
    /// written.
    HeldByOther,
    /// No lock could be taken -- the folder is missing, not writable, or the
    /// filesystem does not support it. Deliberately NOT a refusal to edit: an
    /// advisory lock is cooperative, and a workspace that cannot hold one is
    /// exactly the state this application has always worked in. A folder that
    /// refuses a lock file will refuse the save too, and that path already
    /// reports itself.
    Unavailable,
};

struct LockResult
{
    LockStatus status = LockStatus::Unavailable;
    /// The lock file, whether or not it was taken.
    std::string path;
    /// Who holds it, when HeldByOther and the lock could be read: a program,
    /// a host and a process id. Empty when the holder could not be identified,
    /// which is a normal outcome on a share.
    std::string holder;
    std::string message;
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

    /// Takes the advisory lock on `key`'s file, for FR-ANN-32.
    ///
    /// At most ONE lock is held at a time, because at most one scene is open.
    /// A second lock() replaces the first, and every implementation must make
    /// that true rather than leaking the old one.
    ///
    /// Advisory means cooperative: it stops two copies of THIS application
    /// overwriting each other, and stops nothing else. Returning Unavailable is
    /// not a refusal to work -- see LockStatus.
    virtual LockResult lock(const AnnotationKey& key) = 0;

    /// Releases whatever lock() took. A no-op when nothing is held, so it is
    /// safe to call on every path out, and must be.
    virtual void unlock() = 0;

    /// Where `key` lives, whether or not anything is there. Exists so an error
    /// message can name a real path without the caller rebuilding the naming rule.
    [[nodiscard]] virtual std::string pathFor(const AnnotationKey& key) const = 0;

protected:
    IAnnotationRepository() = default;
};

} // namespace slideio::viewer::core
