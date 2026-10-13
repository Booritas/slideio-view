#pragma once

#include <QKeySequence>
#include <QList>

namespace slideio::viewer::ui
{

/// The shortcuts to install on the Redo action, given whatever bindings the
/// platform itself defines for QKeySequence::Redo.
///
/// Users arrive from applications on other platforms, so Ctrl+Shift+Z is
/// offered everywhere alongside the platform's own key. It cannot simply be
/// appended: QKeySequence::Redo is Ctrl+Y on Windows but Ctrl+Shift+Z on macOS
/// and X11, so appending there would register one sequence twice on a single
/// action. Qt reports a doubly-registered sequence as an ambiguous match and
/// QAction::event warns instead of triggering -- which would leave keyboard
/// Redo dead on exactly the platforms where Ctrl+Shift+Z is the standard key.
///
/// Duplicates are therefore collapsed and empty sequences dropped. The
/// platform's own bindings keep their order and stay ahead of the addition, so
/// the first entry -- the one Qt shows beside the menu item -- remains the
/// platform's own.
[[nodiscard]] QList<QKeySequence> redoShortcuts(const QList<QKeySequence>& platformBindings);

} // namespace slideio::viewer::ui
