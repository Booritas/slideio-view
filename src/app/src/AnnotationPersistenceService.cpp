#include "slideio/viewer/app/AnnotationPersistenceService.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/core/AutosavePolicy.h"

#include <QTimer>

#include <utility>
#include <vector>

namespace slideio::viewer::app
{

namespace
{

/// How often the timer asks the policy. Finer than the debounce so the debounce
/// boundary is honoured to within a tick, coarse enough to cost nothing.
constexpr int kAutosaveTickMs = 500;

} // namespace

AnnotationPersistenceService::AnnotationPersistenceService(core::IAnnotationRepository& repository,
                                                           AnnotationModel& model,
                                                           QObject* parent)
    : QObject(parent)
    , m_repository(repository)
    , m_model(model)
{
    // Selection is deliberately not connected: it is not persisted, and
    // clicking an annotation must not dirty the document or trigger a save.
    connect(&m_model, &AnnotationModel::annotationAdded, this,
            [this](const std::string&) { markDirty(); });
    connect(&m_model, &AnnotationModel::annotationRemoved, this,
            [this](const std::string&) { markDirty(); });
    connect(&m_model, &AnnotationModel::annotationChanged, this,
            [this](const std::string&) { markDirty(); });

    // Not modelReset(): replaceAll() emits that, and this service calls
    // replaceAll() itself during a load, so connecting it would make the
    // service deactivate itself on every successful open. clear() is the
    // signal that means "someone else emptied this".
    connect(&m_model, &AnnotationModel::cleared, this, &AnnotationPersistenceService::onModelCleared);

    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(kAutosaveTickMs);
    connect(m_autosaveTimer, &QTimer::timeout, this, &AnnotationPersistenceService::onAutosaveTick);
}

AnnotationPersistenceService::~AnnotationPersistenceService() = default;

void AnnotationPersistenceService::setMismatchResolver(MismatchResolver resolver)
{
    m_mismatchResolver = std::move(resolver);
}

void AnnotationPersistenceService::setSaveFailureResolver(SaveFailureResolver resolver)
{
    m_saveFailureResolver = std::move(resolver);
}

void AnnotationPersistenceService::beginSlide(const core::AnnotationKey& key,
                                              const core::SlideProvenance& provenance)
{
    // The previous slide's save result is deliberately dropped: endSlide() has
    // already consulted the save-failure resolver, so the user was asked.
    endSlide();

    m_key = key;
    m_provenance = provenance;
    m_createdAt = std::chrono::system_clock::now();
    m_dirty = false;
    m_lastMutation = std::chrono::steady_clock::now();
    m_lastSave = m_lastMutation;

    if (key.slideId.empty()) {
        // Every unidentifiable file produces the same id, so looking one up
        // would hand this slide another slide's annotations.
        m_loading = true;
        m_model.replaceAll({});
        m_loading = false;
        setActive(false);
        return;
    }

    const core::LoadResult loaded = m_repository.load(key);

    switch (loaded.status) {
    case core::LoadStatus::NotFound:
        m_loading = true;
        m_model.replaceAll({});
        m_loading = false;
        setActive(true);
        return;

    case core::LoadStatus::Unreadable:
    case core::LoadStatus::Malformed:
    case core::LoadStatus::UnsupportedVersion:
        m_loading = true;
        m_model.replaceAll({});
        m_loading = false;
        setActive(false);
        emit loadFailed(QString::fromStdString(loaded.path),
                        QString::fromStdString(loaded.message));
        return;

    case core::LoadStatus::Loaded:
        break;
    }

    bool writable = true;
    bool reassociated = false;
    std::vector<core::Annotation> annotations = loaded.document.annotations;

    // Both halves of the key: an absent sceneIndex parses as 0, so comparing the
    // slide id alone would accept scene 0's file for scene 3.
    const core::AnnotationKey storedKey{loaded.document.slideId, loaded.document.sceneIndex};
    if (storedKey != key) {
        const MismatchChoice choice =
            m_mismatchResolver ? m_mismatchResolver(loaded.document, provenance)
                               : MismatchChoice::ReadOnly;
        switch (choice) {
        case MismatchChoice::Ignore:
            annotations.clear();
            writable = false;
            break;
        case MismatchChoice::ReadOnly:
            writable = false;
            break;
        case MismatchChoice::Reassociate:
            // The document adopts this slide's id on the next save; m_key
            // already holds it.
            writable = true;
            reassociated = true;
            break;
        }
    }

    if (loaded.document.createdAt.time_since_epoch().count() != 0) {
        m_createdAt = loaded.document.createdAt;
    }

    m_loading = true;
    m_model.replaceAll(std::move(annotations));
    m_loading = false;
    m_dirty = false;
    // Re-association is a change to the document -- it adopts this slide's id --
    // so it has to be written even if the user never draws anything. Without
    // this the mismatch dialog returns on every open, forever.
    if (reassociated) {
        m_dirty = true;
        m_lastMutation = std::chrono::steady_clock::now();
    }
    setActive(writable);
}

core::SaveResult AnnotationPersistenceService::flush()
{
    if (!m_active || !m_dirty) {
        core::SaveResult result;
        result.status = core::SaveStatus::NothingToDo;
        result.path = m_active ? m_repository.pathFor(m_key) : std::string{};
        return result;
    }

    core::AnnotationDocument document;
    document.schemaVersion = core::kCurrentSchemaVersion;
    document.slideId = m_key.slideId;
    document.sceneIndex = m_key.sceneIndex;
    document.slide = m_provenance;
    document.createdAt = m_createdAt;
    document.modifiedAt = std::chrono::system_clock::now();
    document.annotations = m_model.annotations();

    const core::SaveResult result = m_repository.save(document);
    m_lastSave = std::chrono::steady_clock::now();

    if (result.status == core::SaveStatus::Saved) {
        m_dirty = false;
        emit saved(QString::fromStdString(result.path));
    } else {
        // Left dirty on purpose so the next tick retries -- but move the
        // mutation stamp forward, or "the debounce has already elapsed" stays
        // true and we retry every single tick instead of every two seconds.
        m_lastMutation = std::chrono::steady_clock::now();
        emit saveFailed(QString::fromStdString(result.path),
                        QString::fromStdString(result.message));
    }
    return result;
}

core::SaveResult AnnotationPersistenceService::endSlide()
{
    core::SaveResult result = flush();

    // A close that fails silently is how a slide switch eats an hour of work.
    // The resolver gets a chance to fix the cause and say "try again"; without
    // one, the close proceeds rather than wedging the application.
    //
    // Bounded, so a non-interactive resolver that always says Retry cannot wedge
    // the UI thread; at the cap the close falls through to the discard path.
    constexpr int kMaxRetries = 5;
    const auto isFailure = [](const core::SaveResult& r) {
        return r.status == core::SaveStatus::NotWritable || r.status == core::SaveStatus::Failed;
    };
    for (int attempt = 0; attempt < kMaxRetries && isFailure(result) && m_saveFailureResolver; ++attempt) {
        const int count = static_cast<int>(m_model.annotations().size());
        if (m_saveFailureResolver(result, count) == SaveFailureChoice::Discard) {
            break;
        }
        result = flush();
        if (attempt == kMaxRetries - 1 && isFailure(result)) {
            // Distinct from the per-attempt saveFailed: this one says the
            // service stopped trying and the work is being dropped.
            emit saveAbandoned(QString::fromStdString(result.path),
                               QString::fromStdString(result.message));
        }
    }

    m_autosaveTimer->stop();
    m_key = core::AnnotationKey{};
    m_provenance = core::SlideProvenance{};
    m_dirty = false;
    setActive(false);
    return result;
}

bool AnnotationPersistenceService::isDirty() const
{
    return m_dirty;
}

bool AnnotationPersistenceService::isActive() const
{
    return m_active;
}

void AnnotationPersistenceService::markDirty()
{
    if (m_loading || !m_active) {
        return;
    }
    m_dirty = true;
    m_lastMutation = std::chrono::steady_clock::now();
}

void AnnotationPersistenceService::onModelCleared()
{
    if (!m_active) {
        return;
    }
    // Someone emptied the model while this service was still responsible for
    // writing it. We cannot save what is gone, and writing what remains would
    // put an empty document over the user's file -- so stop being able to
    // write at all, and let the last good save on disk stand.
    //
    // In correct operation this never fires: closeSlide() calls endSlide()
    // before resetAnnotationState(). It is here so that ordering is a safety
    // net rather than a load-bearing assumption.
    m_dirty = false;
    setActive(false);
}

void AnnotationPersistenceService::onAutosaveTick()
{
    if (core::shouldAutosave(m_dirty, m_lastMutation, m_lastSave,
                             std::chrono::steady_clock::now(),
                             core::kAutosaveDebounce, core::kAutosaveBackstop)) {
        flush();
    }
}

void AnnotationPersistenceService::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (active) {
        m_autosaveTimer->start();
    } else {
        m_autosaveTimer->stop();
    }
    emit activeChanged(active);
}

} // namespace slideio::viewer::app
