#include "slideio/viewer/infra/JsonAnnotationRepository.h"

#include "slideio/viewer/core/AnnotationSerialization.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

#include <utility>

namespace slideio::viewer::infra
{

namespace
{

QString annotationsDirectory(const std::string& root)
{
    return QString::fromStdString(root) + QStringLiteral("/annotations");
}

core::LoadResult loadFailure(core::LoadStatus status, std::string path, std::string message)
{
    core::LoadResult result;
    result.status = status;
    result.path = std::move(path);
    result.message = std::move(message);
    return result;
}

core::SaveResult saveFailure(core::SaveStatus status, std::string path, std::string message)
{
    core::SaveResult result;
    result.status = status;
    result.path = std::move(path);
    result.message = std::move(message);
    return result;
}

core::LoadStatus statusForParseError(core::ParseError error)
{
    switch (error) {
    case core::ParseError::UnsupportedFutureVersion:
        return core::LoadStatus::UnsupportedVersion;
    case core::ParseError::MalformedJson:
    case core::ParseError::NotAnAnnotationDocument:
    case core::ParseError::InvalidField:
        return core::LoadStatus::Malformed;
    case core::ParseError::None:
        break;
    }
    return core::LoadStatus::Loaded;
}

} // namespace

JsonAnnotationRepository::JsonAnnotationRepository(std::string workspaceRoot)
    : m_workspaceRoot(std::move(workspaceRoot))
{
}

JsonAnnotationRepository::~JsonAnnotationRepository() = default;

const std::string& JsonAnnotationRepository::workspaceRoot() const
{
    return m_workspaceRoot;
}

void JsonAnnotationRepository::setWorkspaceRoot(std::string workspaceRoot)
{
    m_workspaceRoot = std::move(workspaceRoot);
}

std::string JsonAnnotationRepository::pathFor(const core::AnnotationKey& key) const
{
    const QString name = QStringLiteral("%1.s%2.annotations.json")
                             .arg(QString::fromStdString(key.slideId))
                             .arg(key.sceneIndex);
    return (annotationsDirectory(m_workspaceRoot) + QLatin1Char('/') + name).toStdString();
}

core::LoadResult JsonAnnotationRepository::load(const core::AnnotationKey& key)
{
    const std::string path = pathFor(key);
    const QString qpath = QString::fromStdString(path);

    if (!QFileInfo::exists(qpath)) {
        return loadFailure(core::LoadStatus::NotFound, path, "no annotation file for this slide");
    }

    QFile file(qpath);
    if (!file.open(QIODevice::ReadOnly)) {
        return loadFailure(core::LoadStatus::Unreadable, path, file.errorString().toStdString());
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const core::ParseResult parsed =
        core::parseAnnotationDocument(std::string_view(bytes.constData(),
                                                       static_cast<std::size_t>(bytes.size())));
    if (parsed.error != core::ParseError::None) {
        return loadFailure(statusForParseError(parsed.error), path, parsed.message);
    }

    core::LoadResult result;
    result.status = core::LoadStatus::Loaded;
    result.path = path;
    result.document = parsed.document;
    return result;
}

core::SaveResult JsonAnnotationRepository::save(const core::AnnotationDocument& document)
{
    const std::string path = pathFor({document.slideId, document.sceneIndex});

    // Serialize before touching the filesystem: a document that cannot be
    // represented must not cost the user their previous file.
    const std::string json = core::serializeAnnotationDocument(document);
    if (json.empty()) {
        return saveFailure(core::SaveStatus::Failed, path,
                           "the annotations contain a coordinate JSON cannot represent");
    }

    const QString directory = annotationsDirectory(m_workspaceRoot);
    if (!QDir().mkpath(directory)) {
        return saveFailure(core::SaveStatus::NotWritable, path,
                           "cannot create the annotation folder at "
                               + directory.toStdString());
    }

    // QSaveFile writes to a temporary beside the target and renames on commit,
    // so an interrupted save leaves the previous file intact.
    QSaveFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly)) {
        return saveFailure(core::SaveStatus::NotWritable, path, file.errorString().toStdString());
    }
    if (file.write(json.data(), static_cast<qint64>(json.size())) != static_cast<qint64>(json.size())) {
        file.cancelWriting();
        return saveFailure(core::SaveStatus::Failed, path, file.errorString().toStdString());
    }
    if (!file.commit()) {
        return saveFailure(core::SaveStatus::Failed, path, file.errorString().toStdString());
    }

    core::SaveResult result;
    result.status = core::SaveStatus::Saved;
    result.path = path;
    return result;
}

} // namespace slideio::viewer::infra
