#include "slideio/viewer/app/WorkspaceSwitch.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AnnotationPersistenceService.h"

#include <chrono>
#include <utility>
#include <vector>

namespace slideio::viewer::app
{

namespace
{

bool wasOffered(const core::WorkspaceSwitchOptions& options, WorkspaceChoice choice)
{
    switch (choice) {
    case WorkspaceChoice::Copy:
        return options.copy;
    case WorkspaceChoice::KeepCurrent:
        return options.keepCurrent;
    case WorkspaceChoice::Merge:
        return options.merge;
    case WorkspaceChoice::LoadNew:
    case WorkspaceChoice::Cancel:
        return true;
    }
    return false;
}

/// The set's history starts at the earlier of the two documents'. A zero
/// time_point means "unknown", not "the epoch", so it never wins.
std::chrono::system_clock::time_point earlierCreatedAt(std::chrono::system_clock::time_point a,
                                                       std::chrono::system_clock::time_point b)
{
    const bool aKnown = a.time_since_epoch().count() != 0;
    const bool bKnown = b.time_since_epoch().count() != 0;
    if (!aKnown) {
        return b;
    }
    if (!bKnown) {
        return a;
    }
    return a < b ? a : b;
}

} // namespace

WorkspaceSwitch::WorkspaceSwitch(core::IAnnotationRepository& repository, AnnotationModel& model,
                                 AnnotationPersistenceService& persistence,
                                 std::function<std::string()> currentRoot,
                                 std::function<void(std::string)> repointTo)
    : m_repository(repository)
    , m_model(model)
    , m_persistence(persistence)
    , m_currentRoot(std::move(currentRoot))
    , m_repointTo(std::move(repointTo))
{
}

void WorkspaceSwitch::setResolver(Resolver resolver)
{
    m_resolver = std::move(resolver);
}

WorkspaceSwitchResult WorkspaceSwitch::run(const std::string& newRoot,
                                           const core::AnnotationKey& key,
                                           const core::SlideProvenance& provenance)
{
    WorkspaceSwitchResult result;
    const std::string oldRoot = m_currentRoot();

    core::WorkspaceSwitchFacts facts;
    // Read before the flush: endSlide() deactivates the service, and reading
    // either of these afterwards would describe the switch rather than the
    // slide.
    facts.currentWritable = m_persistence.isActive();
    const std::chrono::system_clock::time_point currentCreatedAt = m_persistence.documentCreatedAt();
    facts.oldPath = m_repository.pathFor(key);

    // 1. Flush to the old root.
    const core::SaveResult flushed = m_persistence.endSlide();
    result.flush = flushed.status;
    facts.flushFailed = flushed.status == core::SaveStatus::NotWritable
                        || flushed.status == core::SaveStatus::Failed;

    // The flush can fail, and the save-failure resolver may rescue it to a
    // folder the user picks mid-failure -- which it makes the workspace on the
    // way out. That is a more recent choice than the one typed into
    // Preferences, and the work has already been written there. Re-pointing
    // somewhere else now would strand the file it just saved in a folder this
    // application never looks at again.
    if (m_currentRoot() != oldRoot) {
        result.rescued = true;
        result.choice = WorkspaceChoice::Cancel;
        m_persistence.beginSlide(key, provenance);
        return result;
    }

    // 2. Capture what is on screen.
    const std::vector<core::Annotation> current = m_model.annotations();
    facts.currentCount = static_cast<int>(current.size());

    // 3. Re-point, then probe. Everything written from here lands in the new
    // folder, which is the point.
    m_repointTo(newRoot);
    const core::LoadResult probe = m_repository.load(key);
    facts.newPath = probe.path.empty() ? m_repository.pathFor(key) : probe.path;
    switch (probe.status) {
    case core::LoadStatus::Loaded:
        facts.newFileExists = true;
        facts.newCount = static_cast<int>(probe.document.annotations.size());
        break;
    case core::LoadStatus::NotFound:
        break;
    case core::LoadStatus::Unreadable:
    case core::LoadStatus::Malformed:
    case core::LoadStatus::UnsupportedVersion:
        facts.newFileExists = true;
        facts.newFileReadable = false;
        break;
    }

    // 4-5. Decide what may be offered, then ask.
    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);
    result.asked = options.ask;
    if (!options.ask || !m_resolver) {
        // Nothing at risk, or nobody to ask. Either way the outcome that writes
        // nothing is the honest one.
        result.choice = WorkspaceChoice::LoadNew;
    } else {
        result.choice = m_resolver(facts, options);
    }

    // 6. Apply.
    if (result.choice == WorkspaceChoice::Cancel) {
        m_repointTo(oldRoot);
        m_persistence.beginSlide(key, provenance);
        return result;
    }

    std::vector<core::Annotation> toWrite;
    bool write = false;
    if (wasOffered(options, result.choice)) {
        switch (result.choice) {
        case WorkspaceChoice::Copy:
        case WorkspaceChoice::KeepCurrent:
            toWrite = current;
            write = true;
            break;
        case WorkspaceChoice::Merge:
            result.merge = core::mergeAnnotations(current, probe.document.annotations);
            toWrite = result.merge.annotations;
            write = true;
            break;
        case WorkspaceChoice::LoadNew:
        case WorkspaceChoice::Cancel:
            break;
        }
    }

    if (write) {
        core::AnnotationDocument document;
        document.schemaVersion = core::kCurrentSchemaVersion;
        document.slideId = key.slideId;
        document.sceneIndex = key.sceneIndex;
        document.slide = provenance;
        document.createdAt = earlierCreatedAt(currentCreatedAt, probe.document.createdAt);
        document.modifiedAt = std::chrono::system_clock::now();
        document.annotations = std::move(toWrite);

        const core::SaveResult saved = m_repository.save(document);
        result.write = saved.status;
        result.writePath = saved.path;
    }

    // 7. Re-enter the slide against whichever root is now in effect. The file is
    // the source of truth; the model is never hand-patched.
    m_persistence.beginSlide(key, provenance);
    return result;
}

} // namespace slideio::viewer::app
