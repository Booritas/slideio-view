#include "slideio/viewer/infra/JsonAnnotationRepository.h"

#include "slideio/viewer/core/AnnotationSerialization.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QString>

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

namespace slideio::viewer::infra
{

namespace
{

QString annotationsDirectory(const std::string& root)
{
    return QString::fromStdString(root) + QStringLiteral("/annotations");
}

/// A slide id becomes a path component, so it must not be able to leave the
/// workspace. computeSlideId produces 16 lowercase hex characters; this accepts
/// a little more than that and nothing that can traverse or escape.
///
/// It cannot be left to the caller: save() takes its id from the document, and
/// parseAnnotationDocument accepts any non-empty string in that field, so a
/// hand-edited file is enough to reach here with "../" in it.
bool isSafeKey(const std::string& slideId, int sceneIndex)
{
    if (slideId.empty() || slideId.size() > 64 || sceneIndex < 0) {
        return false;
    }
    for (const char c : slideId) {
        const bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
                             || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}


/// How old a lock file must be before Qt may treat it as abandoned, when it
/// cannot ask the operating system whether the owning process is alive --
/// which is every lock taken by another machine on a shared workspace. Four
/// hours: long enough that it cannot expire during a reading session, short
/// enough that a remote crash does not block the slide for a working day.
/// A lock left by a dead process on THIS host is detected immediately and does
/// not wait for this at all.
constexpr int kStaleLockMilliseconds = 4 * 60 * 60 * 1000;

/// Who holds a lock, in words, for a message the user reads.
///
/// Every field is optional in practice -- a lock file on a share can be
/// unreadable at the moment it is asked about -- so this says only what it
/// actually learned, and returns empty rather than inventing a holder.
std::string describeHolder(QLockFile& lock)
{
    qint64 pid = 0;
    QString host;
    QString application;
    if (!lock.getLockInfo(&pid, &host, &application)) {
        return {};
    }
    // No brackets here: the message that shows this puts the whole of it in
    // brackets, and a pair of its own read "(... (process 1234))".
    QString who = application;
    if (!host.isEmpty()) {
        who += (who.isEmpty() ? QStringLiteral("on ") : QStringLiteral(" on ")) + host;
    }
    if (pid != 0) {
        who += (who.isEmpty() ? QString() : QStringLiteral(", ")) + QStringLiteral("process ") + QString::number(pid);
    }
    return who.toStdString();
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
    // Unreachable: callers only map a real failure. Failing closed is deliberate, so a
    // future enumerator cannot quietly turn into "loaded".
    return core::LoadStatus::Malformed;
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
    // The lock belongs to a path under the OLD root, so it cannot survive a
    // re-root: keeping it would leave a lock file nothing will ever release and
    // leave the new root unlocked while this instance believed otherwise. The
    // caller re-acquires through beginSlide(), which a workspace switch always
    // calls after repointing.
    unlock();
    m_workspaceRoot = std::move(workspaceRoot);
}


core::LockResult JsonAnnotationRepository::lock(const core::AnnotationKey& key)
{
    // One lock at a time: a second call replaces the first rather than leaking
    // a file nothing will release. The interface promises this; here is where
    // it is kept.
    unlock();

    core::LockResult result;
    // Qt's own convention: the guarded file's name plus ".lock", so the two
    // sort together and anyone looking in the folder can see what is held.
    result.path = pathFor(key) + ".lock";

    if (!isSafeKey(key.slideId, key.sceneIndex)) {
        result.status = core::LockStatus::Unavailable;
        result.message = "The slide identifier is not usable as a file name.";
        return result;
    }

    // A lock file cannot live in a folder that does not exist, and the scene
    // most worth locking is the one nobody has annotated yet. If the folder
    // cannot be made, say Unavailable and let the caller carry on: the save
    // will fail later for the same reason and that path reports itself.
    const QString directory = annotationsDirectory(m_workspaceRoot);
    if (!QDir().mkpath(directory)) {
        result.status = core::LockStatus::Unavailable;
        result.message = "The annotation folder could not be created.";
        return result;
    }

    auto candidate = std::make_unique<QLockFile>(QString::fromStdString(result.path));
    // Qt treats a lock as stale either when its process is gone -- which it can
    // check directly when the lock names THIS host -- or when the file is older
    // than this. The first case is the one that matters after a crash and is
    // immediate whatever this value is. The second is the only answer available
    // for a lock taken on another machine over a share, where no process can be
    // interrogated, so it is set long: stealing a live colleague's lock is the
    // failure this whole feature exists to prevent, and a lock left by a remote
    // crash is a nuisance the user can resolve by deleting the file.
    candidate->setStaleLockTime(kStaleLockMilliseconds);
    // Non-blocking by construction. This runs on the UI thread during a slide
    // open; a blocking acquire would freeze the application for as long as
    // somebody else kept the slide open.
    if (candidate->tryLock(0)) {
        m_lock = std::move(candidate);
        result.status = core::LockStatus::Acquired;
        return result;
    }

    if (candidate->error() == QLockFile::LockFailedError) {
        result.status = core::LockStatus::HeldByOther;
        result.holder = describeHolder(*candidate);
        return result;
    }

    // PermissionError, or no lock support on this filesystem. Not a refusal to
    // work: this application ran without any locking at all until today.
    result.status = core::LockStatus::Unavailable;
    result.message = "The annotation folder does not allow locking.";
    return result;
}

void JsonAnnotationRepository::unlock()
{
    // ~QLockFile unlocks and removes the file. Resetting is the whole operation;
    // it is a no-op when nothing is held, which is what lets every path out call
    // it without asking first.
    m_lock.reset();
}

std::string JsonAnnotationRepository::pathFor(const core::AnnotationKey& key) const
{
    const QString name = QString::fromStdString(key.slideId) + QStringLiteral(".s")
                         + QString::number(key.sceneIndex) + QStringLiteral(".annotations.json");
    return (annotationsDirectory(m_workspaceRoot) + QLatin1Char('/') + name).toStdString();
}

core::LoadResult JsonAnnotationRepository::load(const core::AnnotationKey& key)
{
    const std::string path = pathFor(key);

    if (!isSafeKey(key.slideId, key.sceneIndex)) {
        return loadFailure(core::LoadStatus::Unreadable, path,
                           "the slide id or scene index cannot be used in a file name");
    }
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

    if (!isSafeKey(document.slideId, document.sceneIndex)) {
        return saveFailure(core::SaveStatus::Failed, path, "the slide id or scene index cannot be used in a file name");
    }

    // Serialize before touching the filesystem: a document that cannot be
    // represented must not cost the user their previous file.
    const std::string json = core::serializeAnnotationDocument(document);
    if (json.empty()) {
        return saveFailure(core::SaveStatus::Failed, path,
                           "the annotations contain a value that cannot be written to JSON");
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
