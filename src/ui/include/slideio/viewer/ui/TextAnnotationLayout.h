#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <QtCore/qnamespace.h>
#include <QPointF>
#include <QRectF>

#include <string>
#include <vector>

namespace slideio::viewer::ui
{

/// The size every text annotation is drawn at, in points, whatever the zoom
/// (FR-ANN-10; the UI design's "font size is defined in screen-space points").
inline constexpr double kTextPointSize = 12.0;

/// Screen pixels between a text's anchor and the left edge of its plate.
inline constexpr double kTextGapScreenPixels = 6.0;

/// Screen pixels of plate around the words on every side.
inline constexpr double kTextPlatePaddingScreenPixels = 3.0;

/// The plate a text annotation is drawn on, in screen pixels -- the one
/// rectangle the painter fills and the viewport hit-tests, so what you see is
/// what you click.
///
/// Its left edge is kTextGapScreenPixels right of the anchor, it is centred
/// vertically on the anchor, and it is the measured text block padded by
/// kTextPlatePaddingScreenPixels. Measured by the caller (QFontMetricsF needs
/// an application, which no test has), so this is plain arithmetic; it does
/// not depend on the zoom, which is the point of a screen-sized font. A text
/// with no words still gets the padding, so it stays a target.
[[nodiscard]] QRectF textPlateRect(QPointF anchorScreen, double textWidth, double textHeight);

/// The annotation a press selects when the geometric hit test and the text
/// plates each found one: whichever comes later in `annotations`, which is
/// paint order -- the one drawn on top. Either may be empty.
[[nodiscard]] std::string topmostOf(const std::string& geometricHit, const std::string& textHit,
                                    const std::vector<core::Annotation>& annotations);

/// The faintest a text's white plate is ever drawn. At the default fill
/// opacity alone (0.3) words were unreadable on dark tissue -- the owner could
/// not read them, 2026-10-24 -- and the plate exists to make them readable.
inline constexpr double kTextPlateMinimumOpacity = 0.85;

/// The opacity of a text's white plate for an annotation's `fillOpacity`: never
/// below kTextPlateMinimumOpacity, so a higher fill still makes it more solid.
[[nodiscard]] double textPlateOpacity(double fillOpacity);

/// The text a Text-tool click edits rather than placing a new one on top of:
/// `hitId` when it names a text in `annotations`, otherwise empty. A shape is
/// never a target -- a text placed on a region is how a region gets a label.
[[nodiscard]] std::string textToolEditTarget(const std::string& hitId,
                                             const std::vector<core::Annotation>& annotations);

/// Whether the text editor losing focus for `reason` commits its words.
///
/// Every reason but a popup: a menu or the editor's own context menu opening
/// is the user still at work. Asked from the editor's own FocusOut rather than
/// left to QLineEdit::editingFinished, which Qt 6 emits on focus loss only
/// when the text was edited -- a text opened by double-click and left
/// untouched would otherwise stay open over a slide the user had moved on from.
[[nodiscard]] bool textEditorCommitsOnFocusOut(Qt::FocusReason reason);

} // namespace slideio::viewer::ui
