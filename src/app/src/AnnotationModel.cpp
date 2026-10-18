#include "slideio/viewer/app/AnnotationModel.h"

#include <QUuid>

#include <algorithm>
#include <chrono>
#include <set>
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

std::string AnnotationModel::add(core::AnnotationGeometry geometry)
{
    std::string id = generateId();
    core::AnnotationMetadata metadata;
    metadata.author = m_defaultAuthor;
    const auto now = std::chrono::system_clock::now();
    metadata.createdAt = now;
    metadata.modifiedAt = now;

    core::AnnotationProperties properties;
    // Markers only: see setCurrentCategory.
    if (core::typeOf(geometry) == core::AnnotationType::Point) {
        properties.classification = m_currentCategory;
    }

    m_annotations.emplace_back(id, std::move(geometry), std::move(properties), metadata);
    emit annotationAdded(id);
    return id;
}

void AnnotationModel::insert(core::Annotation annotation)
{
    insertAt(m_annotations.size(), std::move(annotation));
}

std::size_t AnnotationModel::indexOf(const std::string& id) const
{
    for (std::size_t i = 0; i < m_annotations.size(); ++i) {
        if (m_annotations[i].id() == id) {
            return i;
        }
    }
    return kNotFound;
}

void AnnotationModel::insertAt(std::size_t index, core::Annotation annotation)
{
    // An empty id cannot be found, removed or moved, and a duplicate makes
    // hitTest (which scans in reverse) and find (which takes the first match)
    // disagree about which shape the user is pointing at. Both are states this
    // model cannot resolve, so neither is stored.
    if (annotation.id().empty() || find(annotation.id()) != nullptr) {
        return;
    }
    const std::string id = annotation.id();
    const std::size_t at = index > m_annotations.size() ? m_annotations.size() : index;
    m_annotations.insert(m_annotations.begin() + static_cast<std::ptrdiff_t>(at),
                         std::move(annotation));
    emit annotationAdded(id);
}

void AnnotationModel::replaceAll(std::vector<core::Annotation> annotations)
{
    // Entries the model cannot resolve -- an empty id, or an id already kept --
    // are dropped, the same rule insert() applies one at a time.
    std::vector<core::Annotation> kept;
    std::set<std::string> seenIds;
    kept.reserve(annotations.size());
    for (core::Annotation& annotation : annotations) {
        if (!annotation.id().empty() && seenIds.insert(annotation.id()).second) {
            kept.push_back(std::move(annotation));
        }
    }
    m_annotations = std::move(kept);

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

void AnnotationModel::setCurrentCategory(std::string category)
{
    m_currentCategory = std::move(category);
}

const std::string& AnnotationModel::currentCategory() const
{
    return m_currentCategory;
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

bool AnnotationModel::restore(core::Annotation annotation)
{
    // Copied before the search: `annotation` is about to be moved from, and the
    // emit below needs the id afterwards.
    const std::string targetId = annotation.id();

    const auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                                 [&targetId](const core::Annotation& a) { return a.id() == targetId; });
    if (it == m_annotations.end()) {
        return false;
    }

    // Assigned in place rather than erase-and-insert: the position in this
    // vector is paint order and hit-test order, so moving it would change which
    // shape paints on top and which one a click finds.
    *it = std::move(annotation);
    emit annotationChanged(targetId);
    return true;
}

std::size_t AnnotationModel::restoreMany(const std::vector<core::Annotation>& annotations)
{
    std::size_t stored = 0;
    for (const core::Annotation& annotation : annotations) {
        const std::size_t index = indexOf(annotation.id());
        if (index == kNotFound) {
            continue;
        }
        m_annotations[index] = annotation;
        ++stored;
    }
    if (stored > 0) {
        emit modelReset();
    }
    return stored;
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
    // An id this model does not hold cannot be selected. A selection naming
    // nothing renders no handles, matches no delete and survives until
    // something else replaces it, and only the model knows whether an id is
    // real -- so the check belongs here rather than at each caller. The empty
    // id is the deliberate exception: it is how a selection is cleared.
    if (!id.empty() && find(id) == nullptr) {
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
