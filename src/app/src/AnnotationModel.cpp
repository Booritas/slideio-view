#include "slideio/viewer/app/AnnotationModel.h"

#include <QUuid>

#include <algorithm>
#include <utility>

namespace slideio::viewer::app
{

namespace
{

std::string generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

} // namespace

AnnotationModel::AnnotationModel(QObject* parent)
    : QObject(parent)
{
}

AnnotationModel::~AnnotationModel() = default;

std::string AnnotationModel::add(core::AnnotationType type, core::AnnotationGeometry geometry)
{
    std::string id = generateId();
    m_annotations.emplace_back(id, type, std::move(geometry));
    emit annotationAdded(id);
    return id;
}

bool AnnotationModel::remove(const std::string& id)
{
    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&id](const core::Annotation& a) { return a.id() == id; });
    if (it == m_annotations.end()) {
        return false;
    }

    m_annotations.erase(it);
    if (m_selectedId == id) {
        m_selectedId.clear();
        emit selectionChanged(m_selectedId);
    }
    emit annotationRemoved(id);
    return true;
}

bool AnnotationModel::setGeometry(const std::string& id, core::AnnotationGeometry geometry)
{
    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&id](const core::Annotation& a) { return a.id() == id; });
    if (it == m_annotations.end()) {
        return false;
    }

    it->setGeometry(std::move(geometry));
    emit annotationChanged(id);
    return true;
}

const std::vector<core::Annotation>& AnnotationModel::annotations() const
{
    return m_annotations;
}

const core::Annotation* AnnotationModel::find(const std::string& id) const
{
    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&id](const core::Annotation& a) { return a.id() == id; });
    return it == m_annotations.end() ? nullptr : &(*it);
}

std::string AnnotationModel::hitTest(core::PointF point, double toleranceSlideUnits) const
{
    for (auto it = m_annotations.rbegin(); it != m_annotations.rend(); ++it) {
        if (it->containsPoint(point, toleranceSlideUnits)) {
            return it->id();
        }
    }
    return {};
}

void AnnotationModel::setSelected(const std::string& id)
{
    if (m_selectedId == id) {
        return;
    }
    m_selectedId = id;
    emit selectionChanged(m_selectedId);
}

void AnnotationModel::clearSelection()
{
    setSelected(std::string{});
}

const std::string& AnnotationModel::selectedId() const
{
    return m_selectedId;
}

void AnnotationModel::clear()
{
    m_annotations.clear();
    if (!m_selectedId.empty()) {
        m_selectedId.clear();
        emit selectionChanged(m_selectedId);
    }
    emit cleared();
}

} // namespace slideio::viewer::app
