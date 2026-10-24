#pragma once

#include <QDockWidget>

#include <memory>
#include <string>
#include <vector>

class QEvent;
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

    /// The configured category list, as the counting combo offers it and as the
    /// tally orders its rows. "(none)" is always the combo's first item, and what
    /// is current survives the rebuild without being re-sent to the model.
    void setCategories(const std::vector<std::string>& categories);

    /// Gates only the combo. The tally stays visible and correct on a read-only
    /// slide: it still shows its annotations, and what is there is what a reader
    /// opening one wants counted.
    void setCountingEnabled(bool enabled);

    /// Makes `category` the one counted under, in the combo and in the model, without going
    /// through the combo's own handler. Empty selects "(none)". MainWindow calls this after a
    /// rename, because the combo keeps showing the old text when the list under it changes.
    void setCurrentCategory(const std::string& category);

signals:
    /// Rename... was chosen on a tally row. Never emitted for Unclassified. MainWindow does the
    /// work: it owns the store, the model and the undo stack.
    void renameCategoryRequested(const std::string& category);

    /// Remove from list was chosen on a configured tally row. Vocabulary only; no annotation is
    /// touched.
    void removeCategoryRequested(const std::string& category);

    /// A row was double-clicked. MainWindow routes this to the viewport; this
    /// panel never touches the viewport itself, which is the whole point of
    /// moving the model out of it.
    void annotationActivated(const std::string& id);

    /// Delete was pressed on a row. MainWindow routes this through
    /// app::deleteWithUndo so the deletion is undoable -- removing it here
    /// would produce an edit Ctrl+Z cannot reach.
    void deleteRequested(const std::string& id);
    /// Move to Layer was chosen on a row's context menu.
    void moveToLayerRequested(const std::string& id, const std::string& layerId);

    /// An annotation was added carrying a non-empty classification. MainWindow
    /// appends it to the stored list if it is not there: a category earns its
    /// place by having something counted under it, so a typo noticed before the
    /// first click leaves nothing behind.
    void categoryUsed(const std::string& category);

protected:
    /// Watches the counting combo and its line edit so the stepping keys and the
    /// wheel cannot reassign the counting category. The decision is
    /// countingComboSwallowsKey/Wheel, which a test can reach; this does nothing
    /// but ask it and consume the event. See AC26.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    /// Rebuilds the list and the tally from the model. Called on every model
    /// signal except selectionChanged.
    /// A no-op while the dock is hidden; showEvent() catches up.
    void rebuild();
    /// The list alone. What the filter box and the sort combo change: neither
    /// can move a tally count, so neither pays for recomputing one.
    void rebuildRows();
    /// The tally alone, over every annotation in the scene rather than the
    /// filtered rows.
    void rebuildTally();
    /// Moves the highlight to `id` without emitting, so it is not mistaken for
    /// the user clicking a row.
    void applySelection(const std::string& id);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
