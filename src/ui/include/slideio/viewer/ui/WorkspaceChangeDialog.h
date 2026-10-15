#pragma once

#include "slideio/viewer/app/WorkspaceSwitch.h"
#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

#include <QDialog>

namespace slideio::viewer::ui
{

/// Asks what should happen to the open slide's annotations when the annotation
/// workspace folder changes.
///
/// Holds no policy: which buttons exist comes from core::workspaceSwitchOptions,
/// and every number it shows was measured by app::WorkspaceSwitch. A QMessageBox
/// would do for two buttons; this has up to four, each needing a line of
/// explanation under it.
class WorkspaceChangeDialog : public QDialog
{
    Q_OBJECT

public:
    WorkspaceChangeDialog(const core::WorkspaceSwitchFacts& facts,
                          const core::WorkspaceSwitchOptions& options, QWidget* parent = nullptr);

    /// Meaningful after exec(). Rejecting the dialog is Cancel.
    [[nodiscard]] app::WorkspaceChoice choice() const;

private:
    app::WorkspaceChoice m_choice = app::WorkspaceChoice::Cancel;
};

} // namespace slideio::viewer::ui
