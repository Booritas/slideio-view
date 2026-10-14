#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <QDockWidget>

#include <memory>
#include <string>

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

signals:
    /// A field was committed and actually changed something. MainWindow applies
    /// it: it owns the undo stack and the read-only guard, and keeping both in
    /// one place is what stops this panel and the viewport disagreeing about
    /// whether the open slide may be edited.
    void propertiesCommitted(const std::string& id, const core::AnnotationProperties& properties,
                             const std::string& description);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// `force` writes into fields that have focus too; it is for a change of
    /// annotation, where leaving the previous one's text in a focused field
    /// would let a later commit write it onto the new annotation.
    void repopulate(bool force = false);
    void setFieldsEnabled(bool enabled);
    void updateColorSwatch();
    /// Builds the field values, asks propertiesEdit whether anything moved, and
    /// if so emits propertiesCommitted. Does not mutate the model.
    void commit();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
