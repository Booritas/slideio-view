#include "slideio/viewer/ui/AppPaths.h"

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

} // namespace slideio::viewer::ui
