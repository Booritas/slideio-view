#include "slideio/viewer/ui/TextAnnotationLayout.h"

#include <algorithm>
#include <variant>

namespace slideio::viewer::ui
{

QRectF textPlateRect(QPointF anchorScreen, double textWidth, double textHeight)
{
    const double padding = kTextPlatePaddingScreenPixels;
    return QRectF(anchorScreen.x() + kTextGapScreenPixels, anchorScreen.y() - textHeight / 2.0 - padding,
                  textWidth + 2.0 * padding, textHeight + 2.0 * padding);
}

std::string topmostOf(const std::string& geometricHit, const std::string& textHit,
                      const std::vector<core::Annotation>& annotations)
{
    if (geometricHit.empty() || textHit.empty()) {
        return geometricHit.empty() ? textHit : geometricHit;
    }
    // The later in paint order is the one on top.
    for (auto it = annotations.rbegin(); it != annotations.rend(); ++it) {
        if (it->id() == geometricHit || it->id() == textHit) {
            return it->id();
        }
    }
    return geometricHit;
}

double textPlateOpacity(double fillOpacity)
{
    return std::max(fillOpacity, kTextPlateMinimumOpacity);
}

std::string textToolEditTarget(const std::string& hitId, const std::vector<core::Annotation>& annotations)
{
    for (const core::Annotation& annotation : annotations) {
        if (annotation.id() == hitId && std::holds_alternative<core::TextGeometry>(annotation.geometry())) {
            return hitId;
        }
    }
    return {};
}

bool textEditorCommitsOnFocusOut(Qt::FocusReason reason)
{
    return reason != Qt::PopupFocusReason;
}

} // namespace slideio::viewer::ui
