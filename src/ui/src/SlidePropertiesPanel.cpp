#include "slideio/viewer/ui/SlidePropertiesPanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHeaderView>
#include <QLocale>
#include <QMenu>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace slideio::viewer::ui
{

namespace
{

QString dataTypeName(core::DataType dt)
{
    switch (dt) {
    case core::DataType::Byte:    return QStringLiteral("uint8");
    case core::DataType::Int8:    return QStringLiteral("int8");
    case core::DataType::UInt16:  return QStringLiteral("uint16");
    case core::DataType::Int16:   return QStringLiteral("int16");
    case core::DataType::UInt32:  return QStringLiteral("uint32");
    case core::DataType::Int32:   return QStringLiteral("int32");
    case core::DataType::Int64:   return QStringLiteral("int64");
    case core::DataType::UInt64:  return QStringLiteral("uint64");
    case core::DataType::Float16: return QStringLiteral("float16");
    case core::DataType::Float32: return QStringLiteral("float32");
    case core::DataType::Float64: return QStringLiteral("float64");
    case core::DataType::Unknown: return QStringLiteral("unknown");
    case core::DataType::None:    return QStringLiteral("none");
    }
    return QStringLiteral("?");
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("—");
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    return QString::number(v, 'f', (u == 0) ? 0 : 2) + ' ' + units[u]
         + " (" + QLocale::system().toString(bytes) + " bytes)";
}

QString formatDouble(double v, int prec = 3)
{
    return QString::number(v, 'f', prec);
}

void addRow(QTreeWidget* tree, const QString& key, const QString& value)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, key);
    item->setText(1, value);
}

} // namespace

struct SlidePropertiesPanel::Impl
{
    QTreeWidget* tree = nullptr;
};

SlidePropertiesPanel::SlidePropertiesPanel(QWidget* parent)
    : QDockWidget("Properties", parent)
    , m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("SlidePropertiesPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_impl->tree = new QTreeWidget(this);
    m_impl->tree->setColumnCount(2);
    m_impl->tree->setHeaderLabels(QStringList() << "Property" << "Value");
    m_impl->tree->setRootIsDecorated(true);
    m_impl->tree->setUniformRowHeights(true);
    m_impl->tree->setAlternatingRowColors(true);
    m_impl->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_impl->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_impl->tree->header()->setStretchLastSection(true);
    setWidget(m_impl->tree);

    // Right-click anywhere in the tree -> context menu with "Copy properties
    // as text". The action serializes the current rows (including the nested
    // pyramid levels) to a plain-text "Property: Value" listing and puts it
    // on the system clipboard.
    m_impl->tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_impl->tree, &QWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            QMenu menu(m_impl->tree);
            QAction* copyAction = menu.addAction(QStringLiteral("Copy properties as text"));
            copyAction->setEnabled(m_impl->tree->topLevelItemCount() > 0);
            QAction* picked = menu.exec(m_impl->tree->viewport()->mapToGlobal(pos));
            if (picked != copyAction) return;

            QStringList lines;
            const int topCount = m_impl->tree->topLevelItemCount();
            for (int i = 0; i < topCount; ++i) {
                QTreeWidgetItem* item = m_impl->tree->topLevelItem(i);
                lines << QStringLiteral("%1: %2").arg(item->text(0), item->text(1));
                const int childCount = item->childCount();
                for (int j = 0; j < childCount; ++j) {
                    QTreeWidgetItem* child = item->child(j);
                    lines << QStringLiteral("  %1: %2").arg(child->text(0), child->text(1));
                }
            }
            QApplication::clipboard()->setText(lines.join(QChar('\n')));
        });
}

SlidePropertiesPanel::~SlidePropertiesPanel() = default;

void SlidePropertiesPanel::clear()
{
    m_impl->tree->clear();
}

void SlidePropertiesPanel::setSlideInfo(const core::SlideInfo& info)
{
    m_impl->tree->clear();

    QString filePath = QString::fromStdString(info.filePath);
    QString fileName = QFileInfo(filePath).fileName();
    addRow(m_impl->tree, QStringLiteral("File name"),
           fileName.isEmpty() ? filePath : fileName);
    addRow(m_impl->tree, QStringLiteral("File path"),
           filePath.isEmpty() ? QStringLiteral("—") : filePath);
    // Always show the driver id reported by Slide::getDriverId() (cached into
    // info.driverId at open time), regardless of whether the user picked a
    // specific driver in the Open dialog or let SlideIO auto-detect.
    addRow(m_impl->tree, QStringLiteral("Driver"),
           info.driverId.empty() ? QStringLiteral("—")
                                 : QString::fromStdString(info.driverId));
    addRow(m_impl->tree, QStringLiteral("Pixel type"), dataTypeName(info.channelDataType));

    QString magText = (info.magnification > 0.0)
        ? QStringLiteral("%1×").arg(formatDouble(info.magnification, 1))
        : QStringLiteral("—");
    addRow(m_impl->tree, QStringLiteral("Magnification"), magText);

    addRow(m_impl->tree, QStringLiteral("Width (pixels)"),
           QLocale::system().toString(info.width));
    addRow(m_impl->tree, QStringLiteral("Height (pixels)"),
           QLocale::system().toString(info.height));

    // Resolution is in meters/pixel; physical extent in µm = width * res * 1e6.
    if (info.resolutionX > 0.0 && info.resolutionY > 0.0) {
        const double widthUm  = info.width  * info.resolutionX * 1.0e6;
        const double heightUm = info.height * info.resolutionY * 1.0e6;
        addRow(m_impl->tree, QStringLiteral("Width (µm)"),  formatDouble(widthUm, 1));
        addRow(m_impl->tree, QStringLiteral("Height (µm)"), formatDouble(heightUm, 1));
        addRow(m_impl->tree, QStringLiteral("Pixel width (µm)"),
               formatDouble(info.resolutionX * 1.0e6, 4));
        addRow(m_impl->tree, QStringLiteral("Pixel height (µm)"),
               formatDouble(info.resolutionY * 1.0e6, 4));
    } else {
        addRow(m_impl->tree, QStringLiteral("Width (µm)"),  QStringLiteral("(unknown)"));
        addRow(m_impl->tree, QStringLiteral("Height (µm)"), QStringLiteral("(unknown)"));
        addRow(m_impl->tree, QStringLiteral("Pixel width (µm)"),  QStringLiteral("(unknown)"));
        addRow(m_impl->tree, QStringLiteral("Pixel height (µm)"), QStringLiteral("(unknown)"));
    }

    addRow(m_impl->tree, QStringLiteral("Channels"),
           QString::number(info.numChannels));
    addRow(m_impl->tree, QStringLiteral("Z-slices"),
           QString::number(info.numZSlices));
    addRow(m_impl->tree, QStringLiteral("Time frames"),
           QString::number(info.numTFrames));

    addRow(m_impl->tree, QStringLiteral("Compression"),
           info.compression.empty() ? QStringLiteral("(unknown)")
                                    : QString::fromStdString(info.compression));

    // Uncompressed size = width * height * channels * Z * T * bytes-per-sample.
    const qint64 uncompressed =
          static_cast<qint64>(info.width)
        * static_cast<qint64>(info.height)
        * static_cast<qint64>(std::max(1, info.numChannels))
        * static_cast<qint64>(std::max(1, info.numZSlices))
        * static_cast<qint64>(std::max(1, info.numTFrames))
        * static_cast<qint64>(core::dataTypeSize(info.channelDataType));
    addRow(m_impl->tree, QStringLiteral("Uncompressed size"), formatBytes(uncompressed));

    addRow(m_impl->tree, QStringLiteral("Pyramid levels"),
           QString::number(info.levels.size()));

    // Pyramid level breakdown as a collapsible parent.
    if (!info.levels.empty()) {
        auto* root = new QTreeWidgetItem(m_impl->tree);
        root->setText(0, QStringLiteral("Levels"));
        root->setText(1, QStringLiteral("%1 entries").arg(info.levels.size()));
        for (const auto& lvl : info.levels) {
            auto* entry = new QTreeWidgetItem(root);
            entry->setText(0, QStringLiteral("Level %1").arg(lvl.level));
            QString summary = QStringLiteral("%1 × %2  (scale %3, tile %4×%5, %6×%7 tiles)")
                .arg(QLocale::system().toString(lvl.width))
                .arg(QLocale::system().toString(lvl.height))
                .arg(formatDouble(lvl.scale, 4))
                .arg(lvl.tileWidth)
                .arg(lvl.tileHeight)
                .arg(lvl.tilesX)
                .arg(lvl.tilesY);
            entry->setText(1, summary);
        }
        root->setExpanded(true);
    }

    // Colour profile. A slide with no usable ICC profile still gets a row: the
    // absence is the point, because it tells the reader the colours on screen
    // are raw scanner RGB rather than colorimetrically defined.
    const auto& profile = info.colorProfileInfo;
    const QString summary = QString::fromStdString(core::colorProfileSummary(profile));
    if (!profile.present) {
        addRow(m_impl->tree, QStringLiteral("Color profile"), summary);
    } else {
        auto* root = new QTreeWidgetItem(m_impl->tree);
        root->setText(0, QStringLiteral("Color profile"));
        root->setText(1, summary);

        auto addChild = [root](const QString& key, const QString& value) {
            auto* entry = new QTreeWidgetItem(root);
            entry->setText(0, key);
            entry->setText(1, value);
        };
        auto orUnknown = [](const std::string& s) {
            return s.empty() ? QStringLiteral("(unknown)") : QString::fromStdString(s);
        };

        addChild(QStringLiteral("Source"),
                 QString::fromLatin1(core::colorProfileSourceName(profile.source)));
        addChild(QStringLiteral("Description"), orUnknown(profile.description));
        addChild(QStringLiteral("Manufacturer"), orUnknown(profile.manufacturer));
        addChild(QStringLiteral("Model"), orUnknown(profile.model));
        addChild(QStringLiteral("Version"), orUnknown(profile.version));
        addChild(QStringLiteral("Data space"),
                 QString::fromLatin1(core::iccColorSpaceName(profile.dataSpace)));
        addChild(QStringLiteral("Connection space"),
                 QString::fromLatin1(core::iccColorSpaceName(profile.connectionSpace)));
        addChild(QStringLiteral("Intent"),
                 QString::fromLatin1(core::renderingIntentName(profile.intent)));
        addChild(QStringLiteral("Size"), formatBytes(static_cast<qint64>(profile.dataSize)));
        root->setExpanded(true);
    }
}

} // namespace slideio::viewer::ui
