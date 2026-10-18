#include "slideio/viewer/infra/QSettingsAnnotationCategoryStore.h"

#include "slideio/viewer/core/AnnotationCategories.h"

#include <QSettings>
#include <QString>
#include <QStringList>

namespace slideio::viewer::infra
{
namespace
{
constexpr const char* kCategoriesKey = "annotations/categories";
}

std::vector<std::string> QSettingsAnnotationCategoryStore::defaultCategories()
{
    return core::kDefaultAnnotationCategories;
}

QSettingsAnnotationCategoryStore::QSettingsAnnotationCategoryStore()
    : m_settings(std::make_unique<QSettings>())
{
}

QSettingsAnnotationCategoryStore::QSettingsAnnotationCategoryStore(const std::string& iniFilePath)
    : m_settings(std::make_unique<QSettings>(QString::fromStdString(iniFilePath), QSettings::IniFormat))
{
}

QSettingsAnnotationCategoryStore::~QSettingsAnnotationCategoryStore() = default;

std::vector<std::string> QSettingsAnnotationCategoryStore::categories() const
{
    if (!m_settings->contains(kCategoriesKey)) {
        return defaultCategories();
    }
    const QStringList stored = m_settings->value(kCategoriesKey).toStringList();
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(stored.size()));
    for (const QString& entry : stored) {
        result.push_back(entry.toStdString());
    }
    return core::normalizeCategoryList(result);
}

void QSettingsAnnotationCategoryStore::setCategories(const std::vector<std::string>& categories)
{
    QStringList stored;
    for (const std::string& entry : core::normalizeCategoryList(categories)) {
        stored.append(QString::fromStdString(entry));
    }
    m_settings->setValue(kCategoriesKey, stored);
    m_settings->sync();
}

} // namespace slideio::viewer::infra
