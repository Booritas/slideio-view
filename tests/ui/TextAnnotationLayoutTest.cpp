#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/TextAnnotationLayout.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <QPointF>
#include <QRectF>

#include <string>
#include <vector>

using namespace slideio::viewer;
using namespace slideio::viewer::ui;

TEST_CASE("the plate sits to the right of the anchor, centred on it", "[ui][TextAnnotationLayout]")
{
    const QRectF plate = textPlateRect(QPointF(100, 50), 80, 14);
    CHECK(plate == QRectF(100 + kTextGapScreenPixels, 50 - 7 - kTextPlatePaddingScreenPixels,
                          80 + 2 * kTextPlatePaddingScreenPixels, 14 + 2 * kTextPlatePaddingScreenPixels));
}

TEST_CASE("longer words widen the plate to the right only", "[ui][TextAnnotationLayout]")
{
    const QRectF shortPlate = textPlateRect(QPointF(100, 50), 80, 14);
    const QRectF longPlate = textPlateRect(QPointF(100, 50), 160, 14);
    CHECK(longPlate.left() == shortPlate.left());
    CHECK(longPlate.top() == shortPlate.top());
    CHECK(longPlate.width() == shortPlate.width() + 80);
}

TEST_CASE("a text with no words still has a plate to click", "[ui][TextAnnotationLayout]")
{
    const QRectF plate = textPlateRect(QPointF(0, 0), 0, 0);
    CHECK_FALSE(plate.isEmpty());
}

TEST_CASE("the later of two hits is the one a click selects", "[ui][TextAnnotationLayout]")
{
    const std::vector<core::Annotation> annotations = {
        core::Annotation("polygon", core::PolygonGeometry{{{0, 0}, {10, 0}, {5, 9}}, true}),
        core::Annotation("text", core::TextGeometry{core::PointF{2, 2}}),
        core::Annotation("later", core::RectangleGeometry{core::PointF{0, 0}, core::PointF{9, 9}}),
    };
    CHECK(topmostOf("polygon", "text", annotations) == "text");
    CHECK(topmostOf("later", "text", annotations) == "later");
    CHECK(topmostOf("", "text", annotations) == "text");
    CHECK(topmostOf("polygon", "", annotations) == "polygon");
    CHECK(topmostOf("", "", annotations).empty());
}

TEST_CASE("every focus loss but a popup commits the words", "[ui][TextAnnotationLayout]")
{
    // Qt 6 emits editingFinished on focus loss only when the text was edited,
    // so a text opened for editing and left untouched has to be closed here.
    CHECK(textEditorCommitsOnFocusOut(Qt::MouseFocusReason));
    CHECK(textEditorCommitsOnFocusOut(Qt::TabFocusReason));
    CHECK(textEditorCommitsOnFocusOut(Qt::BacktabFocusReason));
    CHECK(textEditorCommitsOnFocusOut(Qt::ActiveWindowFocusReason));
    CHECK(textEditorCommitsOnFocusOut(Qt::ShortcutFocusReason));
    CHECK(textEditorCommitsOnFocusOut(Qt::OtherFocusReason));
    // A menu or the editor's own context menu: the user is still typing.
    CHECK_FALSE(textEditorCommitsOnFocusOut(Qt::PopupFocusReason));
}
