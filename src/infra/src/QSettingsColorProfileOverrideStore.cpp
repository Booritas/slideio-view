#include "slideio/viewer/infra/QSettingsColorProfileOverrideStore.h"

#include <QSettings>
#include <QStringList>

namespace slideio::viewer::infra
{

namespace
{

constexpr auto kGroup = "color/slideProfiles";
constexpr auto kProfilePathKey = "profilePath";
constexpr auto kDisplayNameKey = "slideDisplayName";
constexpr auto kDisplacedKey = "displacedEmbedded";

QString entryKey(const std::string& slideId, const char* field)
{
    return QString("%1/%2/%3")
        .arg(kGroup, QString::fromStdString(slideId), QString::fromLatin1(field));
}

} // namespace

QSettingsColorProfileOverrideStore::QSettingsColorProfileOverrideStore()
    : m_settings(std::make_unique<QSettings>())
{
}

QSettingsColorProfileOverrideStore::QSettingsColorProfileOverrideStore(const QString& iniFilePath)
    : m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

QSettingsColorProfileOverrideStore::~QSettingsColorProfileOverrideStore() = default;

std::optional<core::ColorProfileOverride> QSettingsColorProfileOverrideStore::find(
    const std::string& slideId) const
{
    const QString path = m_settings->value(entryKey(slideId, kProfilePathKey)).toString();
    if (path.isEmpty()) {
        return std::nullopt;
    }

    core::ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = path.toStdString();
    entry.slideDisplayName =
        m_settings->value(entryKey(slideId, kDisplayNameKey)).toString().toStdString();
    entry.displacedEmbedded =
        m_settings->value(entryKey(slideId, kDisplacedKey), false).toBool();
    return entry;
}

void QSettingsColorProfileOverrideStore::set(const core::ColorProfileOverride& entry)
{
    m_settings->setValue(entryKey(entry.slideId, kProfilePathKey),
                         QString::fromStdString(entry.profilePath));
    m_settings->setValue(entryKey(entry.slideId, kDisplayNameKey),
                         QString::fromStdString(entry.slideDisplayName));
    m_settings->setValue(entryKey(entry.slideId, kDisplacedKey), entry.displacedEmbedded);
    m_settings->sync();
}

void QSettingsColorProfileOverrideStore::remove(const std::string& slideId)
{
    m_settings->remove(QString("%1/%2").arg(kGroup, QString::fromStdString(slideId)));
    m_settings->sync();
}

std::vector<core::ColorProfileOverride> QSettingsColorProfileOverrideStore::all() const
{
    std::vector<core::ColorProfileOverride> out;

    m_settings->beginGroup(kGroup);
    const QStringList ids = m_settings->childGroups();
    m_settings->endGroup();

    out.reserve(static_cast<size_t>(ids.size()));
    for (const QString& id : ids) {
        if (auto entry = find(id.toStdString())) {
            out.push_back(*entry);
        }
    }
    return out;
}

} // namespace slideio::viewer::infra
