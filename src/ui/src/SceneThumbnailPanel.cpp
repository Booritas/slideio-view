#include "slideio/viewer/ui/SceneThumbnailPanel.h"

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct SceneThumbnailPanel::Impl
{
    struct ThumbnailItem
    {
        QWidget* container = nullptr;
        QLabel* imageLabel = nullptr;
        QLabel* nameLabel = nullptr;
        core::SceneInfo info;
    };

    SceneThumbnailPanel* owner = nullptr;
    std::vector<ThumbnailItem> sceneItems;
    std::vector<ThumbnailItem> auxItems;
    int activeSceneIndex = 0;
    bool activeIsAux = false;
    int thumbnailSize = 256;

    QScrollArea* scrollArea = nullptr;
    QWidget* contentWidget = nullptr;
    QVBoxLayout* contentLayout = nullptr;

    void buildUi()
    {
        scrollArea = new QScrollArea(owner);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);

        contentWidget = new QWidget(scrollArea);
        contentWidget->setStyleSheet("background-color: #2D2D2D;");
        contentLayout = new QVBoxLayout(contentWidget);
        contentLayout->setContentsMargins(4, 4, 4, 4);
        contentLayout->setSpacing(8);
        contentLayout->addStretch();

        scrollArea->setWidget(contentWidget);
        owner->setWidget(scrollArea);
    }

    void clearItems()
    {
        for (auto& item : sceneItems) {
            if (item.container) {
                item.container->removeEventFilter(owner);
                delete item.container;
            }
        }
        sceneItems.clear();

        for (auto& item : auxItems) {
            if (item.container) {
                item.container->removeEventFilter(owner);
                delete item.container;
            }
        }
        auxItems.clear();

        // Remove all items from layout
        while (contentLayout->count() > 0) {
            QLayoutItem* layoutItem = contentLayout->takeAt(0);
            if (layoutItem->widget() && layoutItem->widget() != contentWidget) {
                delete layoutItem->widget();
            }
            delete layoutItem;
        }
    }

    ThumbnailItem createThumbnailItem(const core::SceneInfo& sceneInfo,
                                      const QString& displayName,
                                      bool isAuxiliary)
    {
        ThumbnailItem item;
        item.info = sceneInfo;

        item.container = new QWidget(contentWidget);
        item.container->setFixedWidth(thumbnailSize);
        item.container->setCursor(Qt::PointingHandCursor);
        item.container->setStyleSheet(
            "QWidget { border: 2px solid transparent; border-radius: 4px; }");

        auto* layout = new QVBoxLayout(item.container);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);

        // Thumbnail image label with gray placeholder
        item.imageLabel = new QLabel(item.container);
        item.imageLabel->setFixedSize(thumbnailSize - 12, thumbnailSize - 12);
        item.imageLabel->setAlignment(Qt::AlignCenter);
        item.imageLabel->setStyleSheet(
            "QLabel { background-color: #3A3A3A; border: 1px solid #555; border-radius: 2px; }");
        layout->addWidget(item.imageLabel, 0, Qt::AlignCenter);

        // Name label
        item.nameLabel = new QLabel(displayName, item.container);
        item.nameLabel->setAlignment(Qt::AlignCenter);
        item.nameLabel->setWordWrap(true);
        item.nameLabel->setStyleSheet("QLabel { color: #CCCCCC; border: none; }");
        layout->addWidget(item.nameLabel);

        // Store metadata as dynamic properties for event filter lookup
        item.container->setProperty("sceneIndex", sceneInfo.index);
        item.container->setProperty("isAuxiliary", isAuxiliary);
        item.container->setProperty("auxiliaryName",
                                    QString::fromStdString(sceneInfo.auxiliaryName));

        item.container->installEventFilter(owner);

        return item;
    }

    void rebuildScenes(const std::vector<core::SceneInfo>& scenes,
                       const std::vector<core::SceneInfo>& auxImages)
    {
        clearItems();

        // Add regular scene thumbnails
        for (size_t i = 0; i < scenes.size(); ++i) {
            const auto& scene = scenes[i];
            QString name = scene.name.empty()
                ? QString("Scene %1").arg(static_cast<int>(i) + 1)
                : QString::fromStdString(scene.name);

            auto item = createThumbnailItem(scene, name, false);
            contentLayout->addWidget(item.container, 0, Qt::AlignHCenter);
            sceneItems.push_back(item);
        }

        // Add auxiliary image thumbnails (label, macro, …). These are matched
        // by auxiliaryName in setThumbnail().
        for (const auto& aux : auxImages) {
            QString name = aux.name.empty()
                ? QString::fromStdString(aux.auxiliaryName)
                : QString::fromStdString(aux.name);
            if (name.isEmpty()) name = QStringLiteral("Aux");

            auto item = createThumbnailItem(aux, name, true);
            contentLayout->addWidget(item.container, 0, Qt::AlignHCenter);
            auxItems.push_back(item);
        }

        contentLayout->addStretch();
    }

    void updateActiveHighlight()
    {
        const QString activeStyle =
            "QWidget { border: 2px solid #4A90D9; border-radius: 4px; }";
        const QString inactiveStyle =
            "QWidget { border: 2px solid transparent; border-radius: 4px; }";

        for (auto& item : sceneItems) {
            bool isActive = !activeIsAux && item.info.index == activeSceneIndex;
            item.container->setStyleSheet(isActive ? activeStyle : inactiveStyle);
        }

        for (auto& item : auxItems) {
            bool isActive = activeIsAux && item.info.index == activeSceneIndex;
            item.container->setStyleSheet(isActive ? activeStyle : inactiveStyle);
        }
    }
};

SceneThumbnailPanel::SceneThumbnailPanel(QWidget* parent)
    : QDockWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;

    setWindowTitle("Scenes");
    setMinimumWidth(200);
    setFeatures(QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);

    QSettings settings;
    m_impl->thumbnailSize = settings.value("sceneThumbnails/size", 256).toInt();

    m_impl->buildUi();
}

SceneThumbnailPanel::~SceneThumbnailPanel() = default;

void SceneThumbnailPanel::setScenes(const std::vector<core::SceneInfo>& scenes,
                                    const std::vector<core::SceneInfo>& auxImages)
{
    m_impl->rebuildScenes(scenes, auxImages);
    m_impl->updateActiveHighlight();
}

void SceneThumbnailPanel::setThumbnail(int sceneIndex, bool isAuxiliary,
                                       const std::string& name, const QImage& thumbnail)
{
    auto& items = isAuxiliary ? m_impl->auxItems : m_impl->sceneItems;
    for (auto& item : items) {
        bool match = isAuxiliary
            ? (item.info.auxiliaryName == name)
            : (item.info.index == sceneIndex);
        if (match) {
            int labelSize = m_impl->thumbnailSize - 12;
            QPixmap pixmap = QPixmap::fromImage(thumbnail).scaled(
                labelSize, labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            item.imageLabel->setPixmap(pixmap);
            break;
        }
    }
}

void SceneThumbnailPanel::setActiveScene(int sceneIndex, bool isAuxiliary)
{
    m_impl->activeSceneIndex = sceneIndex;
    m_impl->activeIsAux = isAuxiliary;
    m_impl->updateActiveHighlight();
}

void SceneThumbnailPanel::clear()
{
    m_impl->clearItems();
    m_impl->contentLayout->addStretch();
    m_impl->activeSceneIndex = 0;
    m_impl->activeIsAux = false;
}

bool SceneThumbnailPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget) {
            QVariant indexVar = widget->property("sceneIndex");
            QVariant auxVar = widget->property("isAuxiliary");

            if (indexVar.isValid() && auxVar.isValid()) {
                bool isAux = auxVar.toBool();
                if (isAux) {
                    QString auxName = widget->property("auxiliaryName").toString();
                    emit auxImageSelected(auxName.toStdString());
                } else {
                    emit sceneSelected(indexVar.toInt());
                }
                return true;
            }
        }
    }

    return QDockWidget::eventFilter(watched, event);
}

} // namespace slideio::viewer::ui
