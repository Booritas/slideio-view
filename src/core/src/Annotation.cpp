#include "slideio/viewer/core/Annotation.h"

#include <utility>

namespace slideio::viewer::core
{

Annotation::Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry)
    : m_id(std::move(id))
    , m_type(type)
    , m_geometry(std::move(geometry))
{
    m_metadata.createdAt = std::chrono::system_clock::now();
    m_metadata.modifiedAt = m_metadata.createdAt;
}

const std::string& Annotation::id() const
{
    return m_id;
}

AnnotationType Annotation::type() const
{
    return m_type;
}

const AnnotationGeometry& Annotation::geometry() const
{
    return m_geometry;
}

const AnnotationProperties& Annotation::properties() const
{
    return m_properties;
}

const AnnotationMetadata& Annotation::metadata() const
{
    return m_metadata;
}

void Annotation::setGeometry(AnnotationGeometry geometry)
{
    m_geometry = std::move(geometry);
    m_metadata.modifiedAt = std::chrono::system_clock::now();
}

void Annotation::setProperties(AnnotationProperties properties)
{
    m_properties = std::move(properties);
    m_metadata.modifiedAt = std::chrono::system_clock::now();
}

RectF Annotation::boundingBox() const
{
    return core::boundingBox(m_geometry);
}

bool Annotation::containsPoint(PointF slidePos, double toleranceSlideUnits) const
{
    return core::hitTest(m_geometry, slidePos, toleranceSlideUnits);
}

} // namespace slideio::viewer::core
