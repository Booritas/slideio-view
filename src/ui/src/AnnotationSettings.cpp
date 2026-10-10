#include "slideio/viewer/ui/AnnotationSettings.h"

#include "slideio/viewer/ui/AppPaths.h"

#include <QSettings>

namespace slideio::viewer::ui
{

namespace
{

constexpr const char* kWorkspaceKey = "annotations/workspaceDirectory";
constexpr const char* kUserNameKey = "annotations/userName";

} // namespace

AnnotationSettings::AnnotationSettings()
    : m_settings(std::make_unique<QSettings>())
{
}

AnnotationSettings::AnnotationSettings(const QString& iniFilePath)
    : m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

AnnotationSettings::~AnnotationSettings() = default;

QString AnnotationSettings::workspaceDirectory() const
{
    const QString stored = m_settings->value(kWorkspaceKey).toString().trimmed();
    if (stored.isEmpty()) {
        return defaultAnnotationWorkspaceDirectory();
    }
    return stored;
}

void AnnotationSettings::setWorkspaceDirectory(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        m_settings->remove(kWorkspaceKey);
    } else {
        m_settings->setValue(kWorkspaceKey, trimmed);
    }
    m_settings->sync();
}

QString AnnotationSettings::userName() const
{
    return m_settings->value(kUserNameKey).toString().trimmed();
}

void AnnotationSettings::setUserName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        m_settings->remove(kUserNameKey);
    } else {
        m_settings->setValue(kUserNameKey, trimmed);
    }
    m_settings->sync();
}

bool AnnotationSettings::hasUserName() const
{
    return !userName().isEmpty();
}

} // namespace slideio::viewer::ui
