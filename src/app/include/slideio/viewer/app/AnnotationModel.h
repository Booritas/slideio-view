#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <QObject>

#include <cstddef>
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
    std::string add(core::AnnotationGeometry geometry);

    /// As add(), with `label` already set when annotationAdded is emitted. A
    /// text is created with its words: adding it empty and setting them after
    /// would show every listener -- the list, autosave -- a text with no words
    /// and rebuild the list twice.
    std::string add(core::AnnotationGeometry geometry, std::string label);

    /// Stores `annotation` under the id it already carries, with its metadata
    /// exactly as given. This is the loading path: `add()` mints a fresh id and
    /// stamps the current time, which would churn every id on every save/load
    /// round trip. Preconditions: the id is non-empty and not already in the
    /// model. An annotation violating either is dropped silently (nothing
    /// stored, nothing emitted), so the model never holds a state the
    /// serializer would write and the parser would refuse.
    void insert(core::Annotation annotation);

    /// Returned by indexOf() when the id is not in the model.
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    /// Position of `id` in creation order, or kNotFound. Both paint order and
    /// hit-test order follow this vector -- painting walks it forward, hitTest
    /// walks it in reverse -- so the position is user-visible, not an
    /// implementation detail.
    [[nodiscard]] std::size_t indexOf(const std::string& id) const;

    /// Stores `annotation` at `index`, clamped to size(). Same preconditions as
    /// insert(): an empty or already-present id is dropped silently. Undo needs
    /// this because re-appending a restored annotation would silently promote it
    /// over everything drawn after it, changing which shape paints on top and
    /// which one a click finds.
    void insertAt(std::size_t index, core::Annotation annotation);

    /// Replaces every annotation and clears the selection, emitting one
    /// modelReset() rather than one annotationAdded() per entry.
    /// Entries with an empty id, or an id already seen earlier in the vector,
    /// are dropped silently (the first occurrence wins), as in insert().
    void replaceAll(std::vector<core::Annotation> annotations);

    /// Stamped into the metadata of annotations created through add().
    /// Self-asserted and unverified: it identifies, it does not authenticate.
    void setDefaultAuthor(std::string author);

    /// Stamped into the classification of POINT markers created through add(),
    /// and of nothing else. The test is on the geometry rather than on the
    /// active tool because the model does not know about tools and should not
    /// learn: the rule then holds for every path that creates a marker.
    ///
    /// Creation only. Changing this never alters an annotation that already
    /// exists; retagging what exists is AnnotationRetagCommand, which is
    /// explicit and undoable.
    ///
    /// "Unclassified" (any case, padded or not) is the absence of a value, so it
    /// is stored as the empty category: stamping it would write a literal that
    /// categoryTally folds into the synthetic row and Rename refuses.
    ///
    /// Session state: it deliberately survives clear(), because a study is
    /// often several slides counted the same way.
    void setCurrentCategory(std::string category);
    [[nodiscard]] const std::string& currentCategory() const;

    /// False when `id` is unknown. Clears the selection if it named `id`.
    bool remove(const std::string& id);

    /// False when `id` is unknown.
    bool setGeometry(const std::string& id, core::AnnotationGeometry geometry);

    /// Puts `annotation` back under the id it already carries, metadata and
    /// all, leaving its position in the vector unchanged. False when that id is
    /// not in the model, in which case nothing is stored and nothing emitted.
    ///
    /// setGeometry() is the wrong tool for putting a previous state back: it
    /// restamps modifiedAt, so reverting an edit would leave the record saying
    /// the mark was last touched at the moment of the gesture that put it back
    /// -- and autosave then writes that to the slide's file. Cancelling a move
    /// with Escape is the case that needs this; the undo stack gets the same
    /// effect from AnnotationEditCommand, which carries whole before/after
    /// states for the same reason.
    bool restore(core::Annotation annotation);

    /// Puts every annotation in `annotations` back under the id it carries,
    /// exactly as restore() does, and emits annotationsBulkChanged() ONCE. Returns how many
    /// were stored; ids the model does not hold are skipped.
    ///
    /// One signal rather than one per annotation because AnnotationListPanel
    /// rebuilds its entire tree on annotationChanged: retagging 500 markers
    /// through 500 restore() calls would rebuild a 500-row tree 500 times.
    std::size_t restoreMany(const std::vector<core::Annotation>& annotations);

    /// Appends every annotation in `annotations`, in order, under the ids they
    /// carry, and emits annotationsBulkChanged() ONCE if any was stored --
    /// never annotationAdded(), for restoreMany's reason: an import of several
    /// hundred annotations would otherwise rebuild the list panel's tree once
    /// per annotation. Each annotation is subject to insert()'s rules: an empty
    /// id, or one already in the model or earlier in `annotations`, is dropped
    /// silently. Returns how many were stored.
    std::size_t insertMany(std::vector<core::Annotation> annotations);

    /// Removes every annotation whose id is in `ids`, unknown ids skipped, and
    /// emits annotationsBulkChanged() ONCE if any was removed -- the inverse of
    /// insertMany(), which is what undoing an import calls. Clears the
    /// selection, emitting selectionChanged() as remove() does, if it named one
    /// of them. Returns how many were removed.
    std::size_t removeMany(const std::vector<std::string>& ids);

    [[nodiscard]] const std::vector<core::Annotation>& annotations() const;

    /// Null when `id` is unknown. The pointer is invalidated by any mutation.
    [[nodiscard]] const core::Annotation* find(const std::string& id) const;

    /// The topmost annotation containing `point`, searched in reverse creation
    /// order so the most recently drawn wins. Empty when none is hit.
    [[nodiscard]] std::string hitTest(core::PointF point, double toleranceSlideUnits) const;

    /// Pass an empty id to clear. Emits only on an actual change. An id the
    /// model does not hold is refused: a selection naming nothing renders no
    /// handles and matches no delete, and this is the only place that can tell.
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
    /// ONLY the geometry moved or resized; nothing a list row displays can
    /// have changed.
    ///
    /// Split out of annotationChanged because a move-drag emits one per mouse
    /// move, and AnnotationListPanel rebuilds its whole tree on
    /// annotationChanged -- several hundred QTreeWidgetItems per frame, to
    /// produce rows identical to the ones already on screen. A list row shows
    /// a label, a type and a classification; setGeometry can change none of
    /// the three, because core::typeOf is derived from the geometry's variant
    /// alternative and a translate or a resize keeps it.
    ///
    /// Everything that DOES care about geometry connects both: the viewport
    /// repaints, the properties panel re-measures perimeter and area, and the
    /// persistence service marks the slide dirty. Only the list panel is
    /// entitled to ignore this one, and that is the entire point of it.
    void annotationGeometryChanged(const std::string& id);
    void selectionChanged(const std::string& id);
    void cleared();
    /// The whole contents changed at once. Listeners must rebuild rather than
    /// track individual ids.
    void modelReset();
    /// Many existing annotations changed in place: rebuild, but this is an
    /// ordinary edit, not a new document. Deliberately NOT modelReset(), which
    /// means "the whole contents changed" and which MainWindow treats as a
    /// reason to clear the undo stack and AnnotationPersistenceService cannot
    /// connect because replaceAll() emits it during the service's own load.
    void annotationsBulkChanged();

private:
    std::vector<core::Annotation> m_annotations;
    std::string m_selectedId;
    std::string m_defaultAuthor;
    std::string m_currentCategory;
};

} // namespace slideio::viewer::app
