#pragma once

#include "slideio/viewer/core/AnnotationRepository.h"

#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <string>

class QTimer;

namespace slideio::viewer::app
{

class AnnotationModel;

/// Owns when annotations are read and written, and whether writing is allowed
/// at all.
///
/// The governing rule lives here, not in the repository, which is stateless and
/// cannot know a previous load failed: **a file we could not parse is never a
/// file we write to.** A failed or read-only load leaves isActive() false, and
/// flush() on an inactive service is a no-op.
class AnnotationPersistenceService : public QObject
{
    Q_OBJECT

public:
    /// What to do about a file whose embedded slide id is not this slide's.
    enum class MismatchChoice
    {
        /// Show the annotations; never write. The default when no resolver is set.
        ReadOnly,
        /// Adopt the open slide's id and allow writing from now on.
        Reassociate,
        /// Discard them; leave the model empty and writing disabled.
        Ignore,
    };

    /// Asked to decide a mismatch. Takes the stored document and the open
    /// slide's provenance so the caller can show the user both names.
    /// Installed by the ui layer; the dialog does not belong in this layer.
    using MismatchResolver =
        std::function<MismatchChoice(const core::AnnotationDocument&, const core::SlideProvenance&)>;

    /// What to do when the flush inside endSlide() fails.
    enum class SaveFailureChoice
    {
        /// Try the same save again -- after the resolver has given the user a
        /// chance to fix the cause, such as choosing another folder.
        Retry,
        /// Proceed with the close and lose the annotations. The default when no
        /// resolver is installed, because a close that cannot be completed is
        /// worse than one that loses work the user was told about.
        Discard,
    };

    /// Asked when a save fails while a slide is closing. This is the dangerous
    /// one: openScene() calls closeSlide() internally, so this fires on a slide
    /// *switch*, which is exactly where work would otherwise disappear with no
    /// dialog in sight.
    using SaveFailureResolver = std::function<SaveFailureChoice(const core::SaveResult&, int annotationCount)>;

    AnnotationPersistenceService(core::IAnnotationRepository& repository,
                                 AnnotationModel& model,
                                 QObject* parent = nullptr);
    ~AnnotationPersistenceService() override;

    AnnotationPersistenceService(const AnnotationPersistenceService&) = delete;
    AnnotationPersistenceService& operator=(const AnnotationPersistenceService&) = delete;

    void setMismatchResolver(MismatchResolver resolver);
    void setSaveFailureResolver(SaveFailureResolver resolver);

    /// Loads this key into the model and begins tracking changes. An empty
    /// slideId never becomes active: computeSlideId yields the same value for
    /// every unidentifiable file, so a caller that cannot identify a slide must
    /// not look anything up.
    void beginSlide(const core::AnnotationKey& key, const core::SlideProvenance& provenance);

    /// Writes now, synchronously, if dirty and active. A no-op otherwise.
    core::SaveResult flush();

    /// flush(), then stop tracking. Safe with no slide open.
    core::SaveResult endSlide();

    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool isActive() const;

signals:
    void loadFailed(const QString& path, const QString& message);
    void saveFailed(const QString& path, const QString& message);
    /// Whether annotations can be created and written right now. The ui layer
    /// enables and disables the drawing tools from this.
    void activeChanged(bool active);

private:
    void markDirty();
    void onAutosaveTick();
    void onModelCleared();
    void setActive(bool active);

    core::IAnnotationRepository& m_repository;
    AnnotationModel& m_model;
    MismatchResolver m_mismatchResolver;
    SaveFailureResolver m_saveFailureResolver;
    QTimer* m_autosaveTimer = nullptr;

    core::AnnotationKey m_key;
    core::SlideProvenance m_provenance;
    std::chrono::system_clock::time_point m_createdAt{};
    bool m_active = false;
    bool m_dirty = false;
    bool m_loading = false;
    std::chrono::steady_clock::time_point m_lastMutation{};
    std::chrono::steady_clock::time_point m_lastSave{};
};

} // namespace slideio::viewer::app
