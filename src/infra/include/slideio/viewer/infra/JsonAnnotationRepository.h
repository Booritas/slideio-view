#pragma once

#include "slideio/viewer/core/AnnotationRepository.h"

#include <string>

namespace slideio::viewer::infra
{

/// Stores one JSON document per (slide, scene) under
/// `<workspaceRoot>/annotations/<slideId>.s<sceneIndex>.annotations.json`.
///
/// The directory is created on the first successful save, never on construction
/// or on a load: an installation that never annotates leaves nothing behind.
class JsonAnnotationRepository : public core::IAnnotationRepository
{
public:
    explicit JsonAnnotationRepository(std::string workspaceRoot);
    ~JsonAnnotationRepository() override;

    core::LoadResult load(const core::AnnotationKey& key) override;
    core::SaveResult save(const core::AnnotationDocument& document) override;
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
};

} // namespace slideio::viewer::infra
