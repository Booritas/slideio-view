#include "slideio/viewer/ui/ShortcutPolicy.h"

namespace slideio::viewer::ui
{

QList<QKeySequence> redoShortcuts(const QList<QKeySequence>& platformBindings)
{
    QList<QKeySequence> result;

    const auto append = [&result](const QKeySequence& sequence) {
        if (sequence.isEmpty() || result.contains(sequence)) {
            return;
        }
        result.append(sequence);
    };

    for (const QKeySequence& binding : platformBindings) {
        append(binding);
    }
    append(QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Z)));

    return result;
}

} // namespace slideio::viewer::ui
