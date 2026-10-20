#include "slideio/viewer/ui/AppPaths.h"

#include <QCoreApplication>
#include <QStandardPaths>

namespace slideio::viewer::ui
{

QString logDirectory()
{
    // AppLocalDataLocation uses organization name + application name. They are
    // expected to be set via QCoreApplication::setOrganizationName and
    // setApplicationName before this is called.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/logs");
}

QString logFilePath()
{
    return logDirectory() + QStringLiteral("/slideio-viewer.log");
}

QString defaultAnnotationWorkspaceDirectory()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!documents.isEmpty()) {
        return documents + QStringLiteral("/") + QCoreApplication::applicationName();
    }

    // Documents is not guaranteed to resolve. On a Linux account with no
    // xdg-user-dirs configuration it comes back empty, and the concatenation
    // above would then have produced "/slideio-viewer" -- a directory at the
    // filesystem root. That is unwritable on any normal account, so every
    // annotation save would fail; and on one where it IS writable it is the
    // wrong place to put a user's clinical findings.
    //
    // AppLocalDataLocation already carries the organization and application
    // names, so the application name is NOT appended again here. It is where
    // the log file already lives, so a user told their annotations are there
    // has somewhere to look.
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

} // namespace slideio::viewer::ui
