#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QCoreApplication>

#include <variant>

namespace slideio::viewer::ui
{

namespace
{

QString rectangleName()
{
    return QCoreApplication::translate("AnnotationShapeName", "Rectangle");
}

QString ellipseName()
{
    return QCoreApplication::translate("AnnotationShapeName", "Ellipse");
}

QString polygonName()
{
    return QCoreApplication::translate("AnnotationShapeName", "Polygon");
}

QString freehandLineName()
{
    return QCoreApplication::translate("AnnotationShapeName", "Freehand Line");
}

struct ShapeNameVisitor
{
    QString operator()(const core::RectangleGeometry&) const
    {
        return rectangleName();
    }

    QString operator()(const core::EllipseGeometry&) const
    {
        return ellipseName();
    }

    QString operator()(const core::PolygonGeometry& polygon) const
    {
        // One geometry, two shapes: the flag is the only difference, and the
        // user's word for them is not the same.
        return polygon.closed ? polygonName() : freehandLineName();
    }
};

} // namespace

QString shapeName(const core::Annotation& annotation)
{
    return std::visit(ShapeNameVisitor{}, annotation.geometry());
}

QString shapeTypeName(core::AnnotationType type)
{
    switch (type) {
    case core::AnnotationType::Rectangle:
        return rectangleName();
    case core::AnnotationType::Ellipse:
        return ellipseName();
    case core::AnnotationType::Polygon:
        return polygonName();
    case core::AnnotationType::Polyline:
        return freehandLineName();
    }
    return QString();
}

} // namespace slideio::viewer::ui
