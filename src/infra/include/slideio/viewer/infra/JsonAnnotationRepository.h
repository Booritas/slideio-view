#pragma once

#include "slideio/viewer/core/AnnotationRepository.h"

#include <memory>
#include <string>

class QLockFile;

namespace slideio::viewer::infra
{

/// Stores one JSON document per (slide, scene) under
/// `<workspaceRoot>/annotations/<slideId>.s<sceneIndex>.annotations.json`.
///
/// The directory is created on the first successful save or on the first lock,
/// never on construction and never on a load. Taking a lock has to create it --
/// a lock file cannot live in a folder that does not exist, and the scene most
/// worth locking is the one nobody has annotated yet, where two instances would
/// otherwise both start from empty and the second would overwrite the first.
/// So an installation that opens a slide and never annotates now leaves an
/// empty `annotations/` folder behind, which is the price of FR-ANN-32.
class JsonAnnotationRepository : public core::IAnnotationRepository
{
public:
    explicit JsonAnnotationRepository(std::string workspaceRoot);
    ~JsonAnnotationRepository() override;

    core::LoadResult load(const core::AnnotationKey& key) override;
    core::SaveResult save(const core::AnnotationDocument& document) override;
    core::LockResult lock(const core::AnnotationKey& key) override;
    void unlock() override;
    [[nodiscard]] std::string pathFor(const core::AnnotationKey& key) const override;

    /// Where the repository is rooted.
    [[nodiscard]] const std::string& workspaceRoot() const;

    /// Re-points at a different workspace. The repository object itself must
    /// outlive every change, because AnnotationPersistenceService holds a
    /// reference to it that is bound at construction -- destroying and
    /// rebuilding the repository when the user picks a new folder would leave
    /// that reference dangling.
    ///
    /// The caller is responsible for flushing to the OLD root first. Every
    /// operation here is synchronous on the UI thread, so there is no in-flight
    /// read or write for a re-root to land in the middle of.
    void setWorkspaceRoot(std::string workspaceRoot);

private:
    std::string m_workspaceRoot;
    /// The advisory lock on the open scene, or null when none is held. Owned
    /// behind a forward declaration so this header stays Qt-free, which is why
    /// the destructor is out of line.
    std::unique_ptr<QLockFile> m_lock;
};

} // namespace slideio::viewer::infra
