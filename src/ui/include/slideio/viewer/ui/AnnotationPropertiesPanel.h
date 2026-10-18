#pragma once

#include "slideio/viewer/core/Annotation.h"

#include "slideio/viewer/core/AnnotationMeasurement.h"

#include <QDockWidget>

#include <memory>
#include <string>
#include <vector>

class QShowEvent;

namespace slideio::viewer::app
{
class AnnotationModel;
}

namespace slideio::viewer::ui
{

/// Docking editor for the selected annotation's label, classification, colour,
/// line width, fill opacity and notes, with its type, author and timestamps
/// shown read-only.
///
/// Entirely selection-driven: with nothing selected every field is cleared and
/// disabled, because a disabled panel still showing the last annotation's label
/// reads as though that annotation were selected.
///
/// Edits commit on editingFinished, on the combo's activation and on the colour
/// dialog's acceptance -- never on textChanged. A command per keystroke would
/// fill the 50-step undo stack from a single label.
class AnnotationPropertiesPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnnotationPropertiesPanel(QWidget* parent = nullptr);
    ~AnnotationPropertiesPanel() override;

    AnnotationPropertiesPanel(const AnnotationPropertiesPanel&) = delete;
    AnnotationPropertiesPanel& operator=(const AnnotationPropertiesPanel&) = delete;

    /// Non-owning; null is a legitimate state, as for the list panel.
    void setAnnotationModel(app::AnnotationModel* model);

    /// Commits whatever is typed in a field but not yet committed. Call it
    /// before anything that empties the model without moving the selection --
    /// closing the slide, opening another, quitting -- because choosing a menu
    /// item does not move focus, so no focus-out will have done it.
    void commitPendingEdit();

    /// Rewrites every field from the model. For a refused edit: the fields
    /// still show what was typed, and this snaps them back to what is stored.
    void revertToModel();

    /// Whether the open slide's annotations may be changed at all. False for a
    /// slide whose annotation file could not be parsed, or that the user chose
    /// to open read-only after a slide-id mismatch: those load but are never
    /// written to.
    ///
    /// The fields are disabled rather than left editable and refused on commit.
    /// MainWindow still guards the commit -- this is the affordance, not the
    /// rule -- but a field that cannot be used should not look as though it can.
    /// The reason is already on the status bar, put there by the same
    /// annotationsAvailableChanged signal that drives this, so a greyed field is
    /// explained rather than mysterious.
    void setEditingAllowed(bool allowed);

    /// Physical size of one pixel on the open slide, from SlideInfo's
    /// resolutionX/resolutionY. A non-positive component means the slide
    /// carries no calibration, and measurements are then shown in pixels
    /// rather than suppressed -- a labelled "12480 px²" is a real number that
    /// can be exported, where a scale bar with no scale would be meaningless.
    void setPixelSize(core::PixelSize pixelSize);

    /// The user's configured category list, offered in the Classification
    /// combo. The combo stays editable, so a classification already on an
    /// annotation is still shown when it is not in the list.
    void setCategories(const std::vector<std::string>& categories);

signals:
    /// A field was committed and actually changed something. MainWindow applies
    /// it: it owns the undo stack and the read-only guard, and keeping both in
    /// one place is what stops this panel and the viewport disagreeing about
    /// whether the open slide may be edited.
    void propertiesCommitted(const std::string& id, const core::AnnotationProperties& properties,
                             const std::string& description);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    /// A no-op while the dock is hidden, apart from forgetting shownId;
    /// showEvent() catches up. `force` writes into fields that have focus too; it is for a change of
    /// annotation, where leaving the previous one's text in a focused field
    /// would let a later commit write it onto the new annotation.
    void repopulate(bool force = false);
    void setFieldsEnabled(bool enabled);
    void updateColorSwatch();
    /// Recomputes the Area and Perimeter/Length rows for `annotation`, choosing
    /// physical or pixel units from the slide's calibration.
    void updateMeasurements(const core::Annotation& annotation);
    /// Builds the field values, asks propertiesEdit whether anything moved, and
    /// if so emits propertiesCommitted. Does not mutate the model.
    void commit();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
