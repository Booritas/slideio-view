#include "slideio/viewer/app/AnnotationModel.h"

#include <QUuid>

#include <algorithm>
#include <chrono>
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
    core::AnnotationMetadata metadata;
    metadata.author = m_defaultAuthor;
    const auto now = std::chrono::system_clock::now();
    metadata.createdAt = now;
    metadata.modifiedAt = now;

    m_annotations.emplace_back(id, type, std::move(geometry), core::AnnotationProperties{}, metadata);
    emit annotationAdded(id);
    return id;
}

void AnnotationModel::insert(core::Annotation annotation)
{
    if (annotation.id().empty()) {
        return;
    }
    const std::string id = annotation.id();
    m_annotations.push_back(std::move(annotation));
    emit annotationAdded(id);
}

void AnnotationModel::replaceAll(std::vector<core::Annotation> annotations)
{
    annotations.erase(std::remove_if(annotations.begin(), annotations.end(),
                                     [](const core::Annotation& a) { return a.id().empty(); }),
                      annotations.end());
    m_annotations = std::move(annotations);

    // Cleared directly rather than through clearSelection(), which would emit
    // selectionChanged before modelReset: a listener would see a selection
    // change for a model it has not been told was replaced yet.
    const bool hadSelection = !m_selectedId.empty();
    m_selectedId.clear();

    emit modelReset();
    if (hadSelection) {
        emit selectionChanged(m_selectedId);
    }
}

void AnnotationModel::setDefaultAuthor(std::string author)
{
    m_defaultAuthor = std::move(author);
}

const std::string& AnnotationModel::defaultAuthor() const
{
    return m_defaultAuthor;
}

bool AnnotationModel::remove(const std::string& id)
{
    // Copy before touching anything: `id` may alias m_selectedId, or an
    // element's own m_id. clear() would empty it, and erase() either shifts
    // the followers down (so it names the wrong annotation) or destroys it
    // outright.
    const std::string removedId = id;

    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&removedId](const core::Annotation& a) { return a.id() == removedId; });
    if (it == m_annotations.end()) {
        return false;
    }

    m_annotations.erase(it);
    if (m_selectedId == removedId) {
        m_selectedId.clear();
        emit selectionChanged(m_selectedId);
    }
    emit annotationRemoved(removedId);
    return true;
}

bool AnnotationModel::setGeometry(const std::string& id, core::AnnotationGeometry geometry)
{
    // Copied for the same reason as in remove(): `id` may alias internal state.
    const std::string targetId = id;

    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&targetId](const core::Annotation& a) { return a.id() == targetId; });
    if (it == m_annotations.end()) {
        return false;
    }

    it->setGeometry(std::move(geometry));
    emit annotationChanged(targetId);
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
