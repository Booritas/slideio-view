#pragma once

#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"

#include <chrono>
#include <string>

namespace slideio::viewer::core
{

enum class AnnotationType
{
    Rectangle,
};

struct AnnotationProperties
{
    std::string label;
    std::string classification;
    /// FR-ANN-15's colorblind-safe palette; this is its orange.
    Color color{0xE6, 0x7E, 0x22, 0xFF};
    /// Screen pixels, not slide pixels (FR-ANN-11: 1-5 screen pixels).
    float lineWidth = 2.0f;
    float fillOpacity = 0.3f;
    std::string notes;
};

struct AnnotationMetadata
{
    /// Empty until user identity lands. FR-ANN-12 requires an author and
    /// FR-USER-01 blocks creation without a configured identity, but user
    /// identity is Stage 4 item 8. The field exists now so the struct does not
    /// change when annotation persistence populates it.
    std::string author;
    std::chrono::system_clock::time_point createdAt{};
    std::chrono::system_clock::time_point modifiedAt{};
};

class Annotation
{
public:
    Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry);

    /// Restores an annotation exactly as stored, including its timestamps.
    /// Loading must not restamp createdAt/modifiedAt the way the constructor
    /// above does, and creation must be able to supply an author.
    Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry,
               AnnotationProperties properties, AnnotationMetadata metadata);

    [[nodiscard]] const std::string& id() const;
    [[nodiscard]] AnnotationType type() const;
    [[nodiscard]] const AnnotationGeometry& geometry() const;
    [[nodiscard]] const AnnotationProperties& properties() const;
    [[nodiscard]] const AnnotationMetadata& metadata() const;

    void setGeometry(AnnotationGeometry geometry);
    void setProperties(AnnotationProperties properties);

    [[nodiscard]] RectF boundingBox() const;
    [[nodiscard]] bool containsPoint(PointF slidePos, double toleranceSlideUnits) const;

private:
    std::string m_id;
    AnnotationType m_type;
    AnnotationGeometry m_geometry;
    AnnotationProperties m_properties;
    AnnotationMetadata m_metadata;
};

} // namespace slideio::viewer::core
