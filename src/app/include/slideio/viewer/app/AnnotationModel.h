#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <QObject>

#include <string>
#include <vector>

namespace slideio::viewer::app
{

/// In-memory store of the open slide's annotations.
///
/// Lives in the application layer rather than core because it generates ids
/// with QUuid and emits Qt signals; core is Qt-free. Storage is a flat vector
/// searched linearly -- at the hundreds-of-annotations scale a spatial index
/// buys nothing, and adding one later does not change this interface.
class AnnotationModel : public QObject
{
    Q_OBJECT

public:
    explicit AnnotationModel(QObject* parent = nullptr);
    ~AnnotationModel() override;

    AnnotationModel(const AnnotationModel&) = delete;
    AnnotationModel& operator=(const AnnotationModel&) = delete;

    /// Stores a new annotation under a freshly generated id, which is returned.
    std::string add(core::AnnotationType type, core::AnnotationGeometry geometry);

    /// Stores `annotation` under the id it already carries, with its metadata
    /// exactly as given. This is the loading path: `add()` mints a fresh id and
    /// stamps the current time, which would churn every id on every save/load
    /// round trip. Preconditions: the id is non-empty and not already in the
    /// model. An annotation violating either is dropped silently (nothing
    /// stored, nothing emitted), so the model never holds a state the
    /// serializer would write and the parser would refuse.
    void insert(core::Annotation annotation);

    /// Replaces every annotation and clears the selection, emitting one
    /// modelReset() rather than one annotationAdded() per entry.
    /// Entries with an empty id, or an id already seen earlier in the vector,
    /// are dropped silently (the first occurrence wins), as in insert().
    void replaceAll(std::vector<core::Annotation> annotations);

    /// Stamped into the metadata of annotations created through add().
    /// Self-asserted and unverified: it identifies, it does not authenticate.
    void setDefaultAuthor(std::string author);

    /// False when `id` is unknown. Clears the selection if it named `id`.
    bool remove(const std::string& id);

    /// False when `id` is unknown.
    bool setGeometry(const std::string& id, core::AnnotationGeometry geometry);

    [[nodiscard]] const std::vector<core::Annotation>& annotations() const;

    /// Null when `id` is unknown. The pointer is invalidated by any mutation.
    [[nodiscard]] const core::Annotation* find(const std::string& id) const;

    /// The topmost annotation containing `point`, searched in reverse creation
    /// order so the most recently drawn wins. Empty when none is hit.
    [[nodiscard]] std::string hitTest(core::PointF point, double toleranceSlideUnits) const;

    /// Pass an empty id to clear. Emits only on an actual change. An id not in the
    /// model is accepted as-is (not validated), so that clearSelection() keeps working.
    void setSelected(const std::string& id);
    void clearSelection();
    [[nodiscard]] const std::string& selectedId() const;

    /// Drops every annotation and the selection. Called whenever the open slide
    /// changes: annotations must never survive into a different slide, and
    /// nothing here is persisted, so a stale model is pure misassociation.
    void clear();

signals:
    void annotationAdded(const std::string& id);
    void annotationRemoved(const std::string& id);
    void annotationChanged(const std::string& id);
    void selectionChanged(const std::string& id);
    void cleared();
    /// The whole contents changed at once. Listeners must rebuild rather than
    /// track individual ids.
    void modelReset();

private:
    std::vector<core::Annotation> m_annotations;
    std::string m_selectedId;
    std::string m_defaultAuthor;
};

} // namespace slideio::viewer::app
