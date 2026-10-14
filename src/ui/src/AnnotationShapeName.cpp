#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QCoreApplication>

#include <variant>

namespace slideio::viewer::ui
{

namespace
{

struct ShapeNameVisitor
{
    QString operator()(const core::RectangleGeometry&) const
    {
        return QCoreApplication::translate("AnnotationShapeName", "Rectangle");
    }

    QString operator()(const core::PolygonGeometry& polygon) const
    {
        // One geometry, two shapes: the flag is the only difference, and the
        // user's word for them is not the same.
        return polygon.closed
                   ? QCoreApplication::translate("AnnotationShapeName", "Polygon")
                   : QCoreApplication::translate("AnnotationShapeName", "Freehand Line");
    }
};

} // namespace

QString shapeName(const core::Annotation& annotation)
{
    return std::visit(ShapeNameVisitor{}, annotation.geometry());
}

} // namespace slideio::viewer::ui
