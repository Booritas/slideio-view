#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/ShortcutPolicy.h"

using namespace slideio::viewer::ui;

namespace
{

/// Built from key combinations rather than parsed from strings: no test binary
/// here creates a QGuiApplication, and this needs no platform theme.
QKeySequence ctrlY()
{
    return QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Y));
}

QKeySequence ctrlShiftZ()
{
    return QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Z));
}

} // namespace

TEST_CASE("the platform's own binding is kept and Ctrl+Shift+Z added after it",
          "[ui][ShortcutPolicy]")
{
    // Windows: QKeySequence::Redo is Ctrl+Y, so both keys end up registered.
    const QList<QKeySequence> result = redoShortcuts({ctrlY()});

    REQUIRE(result.size() == 2);
    REQUIRE(result.at(0) == ctrlY());
    REQUIRE(result.at(1) == ctrlShiftZ());
}

TEST_CASE("Ctrl+Shift+Z is not registered twice where it is the platform binding",
          "[ui][ShortcutPolicy]")
{
    // macOS and X11: Ctrl+Shift+Z is already QKeySequence::Redo. A second
    // registration of it makes Qt call the match ambiguous and refuse to
    // trigger, so Redo would stop responding to the keyboard entirely.
    const QList<QKeySequence> result = redoShortcuts({ctrlShiftZ()});

    REQUIRE(result.size() == 1);
    REQUIRE(result.at(0) == ctrlShiftZ());
}

TEST_CASE("a platform that lists both keys produces exactly those two",
          "[ui][ShortcutPolicy]")
{
    const QList<QKeySequence> result = redoShortcuts({ctrlY(), ctrlShiftZ()});

    REQUIRE(result.size() == 2);
    REQUIRE(result.at(0) == ctrlY());
    REQUIRE(result.at(1) == ctrlShiftZ());
}

TEST_CASE("a repeated platform binding is collapsed", "[ui][ShortcutPolicy]")
{
    const QList<QKeySequence> result = redoShortcuts({ctrlY(), ctrlY()});

    REQUIRE(result.size() == 2);
    REQUIRE(result.at(0) == ctrlY());
    REQUIRE(result.at(1) == ctrlShiftZ());
}

TEST_CASE("an empty sequence is dropped rather than registered",
          "[ui][ShortcutPolicy]")
{
    // QKeySequence::keyBindings can come back empty for a platform that
    // defines no Redo key; an empty sequence registered on an action matches
    // nothing and only clutters the menu item's shortcut text.
    const QList<QKeySequence> result = redoShortcuts({QKeySequence()});

    REQUIRE(result.size() == 1);
    REQUIRE(result.at(0) == ctrlShiftZ());
}

TEST_CASE("no platform binding at all still leaves Ctrl+Shift+Z",
          "[ui][ShortcutPolicy]")
{
    const QList<QKeySequence> result = redoShortcuts({});

    REQUIRE(result.size() == 1);
    REQUIRE(result.at(0) == ctrlShiftZ());
}
