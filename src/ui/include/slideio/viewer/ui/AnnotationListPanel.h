#pragma once

#include <QDockWidget>

#include <memory>
#include <string>

class QKeyEvent;
class QShowEvent;

namespace slideio::viewer::app
{
class AnnotationModel;
}

namespace slideio::viewer::ui
{

/// Docking list of the open scene's annotations, with a filter and a sort.
///
/// Holds no state of its own beyond the filter text and the sort key: the rows
/// are rebuilt from the model on every change, and what they say is decided by
/// annotationRows() rather than here. That is deliberate -- no test binary
/// creates a QApplication, so nothing inside this class can be asserted on, and
/// the way to keep it correct is to give it as little to decide as possible.
class AnnotationListPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnnotationListPanel(QWidget* parent = nullptr);
    ~AnnotationListPanel() override;

    AnnotationListPanel(const AnnotationListPanel&) = delete;
    AnnotationListPanel& operator=(const AnnotationListPanel&) = delete;

    /// Non-owning, and null is a legitimate state: a panel with no model shows
    /// nothing. MainWindow owns the model and sets this once, after
    /// construction -- which is the window this null tolerance covers, since a
    /// restored dock layout can make this panel visible before the call.
    void setAnnotationModel(app::AnnotationModel* model);

signals:
    /// A row was double-clicked. MainWindow routes this to the viewport; this
    /// panel never touches the viewport itself, which is the whole point of
    /// moving the model out of it.
    void annotationActivated(const std::string& id);

    /// Delete was pressed on a row. MainWindow routes this through
    /// app::deleteWithUndo so the deletion is undoable -- removing it here
    /// would produce an edit Ctrl+Z cannot reach.
    void deleteRequested(const std::string& id);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    /// Rebuilds every row from the model. Called on every model signal except
    /// selectionChanged.
    /// A no-op while the dock is hidden; showEvent() catches up.
    void rebuild();
    /// Moves the highlight to `id` without emitting, so it is not mistaken for
    /// the user clicking a row.
    void applySelection(const std::string& id);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
