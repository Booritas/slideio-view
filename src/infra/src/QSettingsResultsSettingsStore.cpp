#include "slideio/viewer/infra/QSettingsResultsSettingsStore.h"

#include "slideio/viewer/core/AnnotationCategories.h"

#include <QByteArray>
#include <QSettings>
#include <QStringList>

namespace slideio::viewer::infra
{

namespace
{

constexpr auto kAttachments = "results/attachments";
constexpr auto kClassColours = "results/classColours";

QString entryPath(const std::string& slideId, int scene)
{
    return QString("%1/%2.s%3").arg(kAttachments, QString::fromStdString(slideId)).arg(scene);
}

QString toHex(core::Color c)
{
    return QString("#%1%2%3")
        .arg(c.r, 2, 16, QChar('0'))
        .arg(c.g, 2, 16, QChar('0'))
        .arg(c.b, 2, 16, QChar('0'));
}

std::optional<core::Color> fromHex(const QString& text)
{
    if (text.size() != 7 || text[0] != QChar('#')) {
        return std::nullopt;
    }
    bool ok = false;
    const uint value = text.mid(1).toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    return core::Color{static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF),
                       static_cast<uint8_t>(value & 0xFF), 255};
}

} // namespace

QSettingsResultsSettingsStore::QSettingsResultsSettingsStore() : m_settings(std::make_unique<QSettings>()) {}

QSettingsResultsSettingsStore::QSettingsResultsSettingsStore(const std::string& iniFilePath)
    : m_settings(std::make_unique<QSettings>(QString::fromStdString(iniFilePath), QSettings::IniFormat))
{
}

QSettingsResultsSettingsStore::~QSettingsResultsSettingsStore() = default;

std::optional<core::ResultsAttachmentSettings> QSettingsResultsSettingsStore::find(const std::string& slideId,
                                                                                   int scene) const
{
    if (slideId.empty()) {
        return std::nullopt;
    }
    const QString base = entryPath(slideId, scene);
    if (!m_settings->contains(base + "/workspacePath")) {
        return std::nullopt;
    }
    core::ResultsAttachmentSettings out;
    out.slideId = slideId;
    out.scene = scene;
    out.workspacePath = m_settings->value(base + "/workspacePath").toString().toStdString();
    out.attachAnywaySlidePath = m_settings->value(base + "/attachAnywaySlidePath").toString().toStdString();

    m_settings->beginGroup(base);
    const int layerCount = m_settings->beginReadArray("layers");
    for (int i = 0; i < layerCount; ++i) {
        m_settings->setArrayIndex(i);
        core::ResultsLayerView layer;
        layer.layerName = m_settings->value("name").toString().toStdString();
        layer.visible = m_settings->value("visible", true).toBool();
        layer.colour = fromHex(m_settings->value("colour").toString());
        layer.fill = m_settings->value("fill", false).toBool();
        bool ok = false;
        const double opacity = m_settings->value("fillOpacity", 0.30).toDouble(&ok);
        layer.fillOpacity = ok ? opacity : 0.30;
        layer.colourByClass = m_settings->value("colourByClass", true).toBool();
        const int hiddenCount = m_settings->beginReadArray("hidden");
        for (int j = 0; j < hiddenCount; ++j) {
            m_settings->setArrayIndex(j);
            layer.hiddenClasses.push_back(m_settings->value("name").toString().toStdString());
        }
        m_settings->endArray();
        out.layers.push_back(std::move(layer));
    }
    m_settings->endArray();
    m_settings->endGroup();
    return out;
}

bool QSettingsResultsSettingsStore::set(const core::ResultsAttachmentSettings& entry)
{
    if (entry.slideId.empty()) {
        return false;
    }
    const QString base = entryPath(entry.slideId, entry.scene);
    m_settings->remove(base);
    m_settings->setValue(base + "/workspacePath", QString::fromStdString(entry.workspacePath));
    m_settings->setValue(base + "/attachAnywaySlidePath", QString::fromStdString(entry.attachAnywaySlidePath));

    m_settings->beginGroup(base);
    m_settings->beginWriteArray("layers", static_cast<int>(entry.layers.size()));
    for (int i = 0; i < static_cast<int>(entry.layers.size()); ++i) {
        const core::ResultsLayerView& layer = entry.layers[static_cast<size_t>(i)];
        m_settings->setArrayIndex(i);
        m_settings->setValue("name", QString::fromStdString(layer.layerName));
        m_settings->setValue("visible", layer.visible);
        if (layer.colour) {
            m_settings->setValue("colour", toHex(*layer.colour));
        }
        m_settings->setValue("fill", layer.fill);
        m_settings->setValue("fillOpacity", layer.fillOpacity);
        m_settings->setValue("colourByClass", layer.colourByClass);
        m_settings->beginWriteArray("hidden", static_cast<int>(layer.hiddenClasses.size()));
        for (int j = 0; j < static_cast<int>(layer.hiddenClasses.size()); ++j) {
            m_settings->setArrayIndex(j);
            m_settings->setValue("name", QString::fromStdString(layer.hiddenClasses[static_cast<size_t>(j)]));
        }
        m_settings->endArray();
    }
    m_settings->endArray();
    m_settings->endGroup();
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

void QSettingsResultsSettingsStore::remove(const std::string& slideId, int scene)
{
    if (slideId.empty()) {
        return;
    }
    m_settings->remove(entryPath(slideId, scene));
    m_settings->sync();
}

std::map<std::string, core::Color> QSettingsResultsSettingsStore::classColours() const
{
    std::map<std::string, core::Color> out;
    m_settings->beginGroup(kClassColours);
    const QStringList keys = m_settings->childKeys();
    for (const QString& key : keys) {
        if (auto colour = fromHex(m_settings->value(key).toString())) {
            const QByteArray decoded = QByteArray::fromPercentEncoding(key.toLatin1());
            out[decoded.toStdString()] = *colour;
        }
    }
    m_settings->endGroup();
    return out;
}

void QSettingsResultsSettingsStore::setClassColour(const std::string& className, core::Color colour)
{
    const std::string key = core::caseFoldKey(className);
    if (key.empty()) {
        return;
    }
    // A class name may hold a slash, which QSettings reads as a group
    // separator; percent-encode the key so any name stays one key.
    const QString encoded = QString::fromLatin1(QByteArray::fromStdString(key).toPercentEncoding());
    m_settings->setValue(QString("%1/%2").arg(kClassColours, encoded), toHex(colour));
    m_settings->sync();
}

} // namespace slideio::viewer::infra
