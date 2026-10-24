#include "slideio/viewer/infra/QSettingsCalibrationOverrideStore.h"

#include <QSettings>
#include <QStringList>

namespace slideio::viewer::infra
{

namespace
{

constexpr auto kGroup = "calibration/overrides";
constexpr auto kXKey = "x";
constexpr auto kYKey = "y";
constexpr auto kDisplayNameKey = "slideDisplayName";

/// `<slideId>.s<sceneIndex>`, the annotation file's own key shape.
QString entryName(const std::string& slideId, int sceneIndex)
{
    return QString("%1.s%2").arg(QString::fromStdString(slideId)).arg(sceneIndex);
}

QString entryKey(const std::string& slideId, int sceneIndex, const char* field)
{
    return QString("%1/%2/%3").arg(kGroup, entryName(slideId, sceneIndex), QString::fromLatin1(field));
}

/// The inverse of entryName, or false for a group that is not one of ours.
bool parseEntryName(const QString& name, std::string& slideId, int& sceneIndex)
{
    const int dot = name.lastIndexOf(QStringLiteral(".s"));
    if (dot <= 0) {
        return false;
    }
    bool ok = false;
    const int scene = name.mid(dot + 2).toInt(&ok);
    if (!ok || scene < 0) {
        return false;
    }
    slideId = name.left(dot).toStdString();
    sceneIndex = scene;
    return true;
}

} // namespace

QSettingsCalibrationOverrideStore::QSettingsCalibrationOverrideStore()
    : m_settings(std::make_unique<QSettings>())
{
}

QSettingsCalibrationOverrideStore::QSettingsCalibrationOverrideStore(const std::string& iniFilePath)
    : m_settings(std::make_unique<QSettings>(QString::fromStdString(iniFilePath), QSettings::IniFormat))
{
}

QSettingsCalibrationOverrideStore::~QSettingsCalibrationOverrideStore() = default;

std::optional<core::CalibrationOverride> QSettingsCalibrationOverrideStore::find(const std::string& slideId,
                                                                                 int sceneIndex) const
{
    if (slideId.empty()) {
        return std::nullopt;
    }
    bool okX = false;
    bool okY = false;
    const double x = m_settings->value(entryKey(slideId, sceneIndex, kXKey)).toDouble(&okX);
    const double y = m_settings->value(entryKey(slideId, sceneIndex, kYKey)).toDouble(&okY);
    // A hand-edited registry is the only way here; such a value is not used.
    if (!okX || !okY || !core::isValidCalibrationOverride(core::PixelSize{x, y})) {
        return std::nullopt;
    }
    core::CalibrationOverride entry;
    entry.slideId = slideId;
    entry.sceneIndex = sceneIndex;
    entry.micronsPerPixel = core::PixelSize{x, y};
    entry.slideDisplayName =
        m_settings->value(entryKey(slideId, sceneIndex, kDisplayNameKey)).toString().toStdString();
    return entry;
}

bool QSettingsCalibrationOverrideStore::set(const core::CalibrationOverride& entry)
{
    if (entry.slideId.empty() || !core::isValidCalibrationOverride(entry.micronsPerPixel)) {
        return false;
    }
    m_settings->setValue(entryKey(entry.slideId, entry.sceneIndex, kXKey), entry.micronsPerPixel.x);
    m_settings->setValue(entryKey(entry.slideId, entry.sceneIndex, kYKey), entry.micronsPerPixel.y);
    m_settings->setValue(entryKey(entry.slideId, entry.sceneIndex, kDisplayNameKey),
                         QString::fromStdString(entry.slideDisplayName));
    m_settings->sync();
    return true;
}

void QSettingsCalibrationOverrideStore::remove(const std::string& slideId, int sceneIndex)
{
    if (slideId.empty()) {
        return;
    }
    m_settings->remove(QString("%1/%2").arg(kGroup, entryName(slideId, sceneIndex)));
    m_settings->sync();
}

std::vector<core::CalibrationOverride> QSettingsCalibrationOverrideStore::all() const
{
    m_settings->beginGroup(kGroup);
    const QStringList names = m_settings->childGroups();
    m_settings->endGroup();

    std::vector<core::CalibrationOverride> out;
    out.reserve(static_cast<size_t>(names.size()));
    for (const QString& name : names) {
        std::string slideId;
        int sceneIndex = 0;
        if (!parseEntryName(name, slideId, sceneIndex)) {
            continue;
        }
        if (auto entry = find(slideId, sceneIndex)) {
            out.push_back(*entry);
        }
    }
    return out;
}

} // namespace slideio::viewer::infra
