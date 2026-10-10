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

    /// Pass an empty id to clear. Emits only on an actual change.
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

private:
    std::vector<core::Annotation> m_annotations;
    std::string m_selectedId;
};

} // namespace slideio::viewer::app
