#include "slideio/viewer/ui/ZTNavigationWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct ZTNavigationWidget::Impl
{
    QLabel* zLabel = nullptr;
    QSlider* zSlider = nullptr;
    QLabel* zValueLabel = nullptr;
    QLabel* tLabel = nullptr;
    QSlider* tSlider = nullptr;
    QLabel* tValueLabel = nullptr;
    QWidget* zRow = nullptr;
    QWidget* tRow = nullptr;
    int numZSlices = 1;
    int numTFrames = 1;
    bool updatingValues = false;
};

ZTNavigationWidget::ZTNavigationWidget(QWidget* parent)
    : QWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setAttribute(Qt::WA_TranslucentBackground);

    setStyleSheet(
        "QWidget#ZTNavigationBg {"
        "  background: rgba(45,45,45,200);"
        "  border-radius: 4px;"
        "}");

    auto* bg = new QWidget(this);
    bg->setObjectName("ZTNavigationBg");

    // Z row
    m_impl->zRow = new QWidget(bg);
    m_impl->zLabel = new QLabel("Z (1)", m_impl->zRow);
    m_impl->zSlider = new QSlider(Qt::Horizontal, m_impl->zRow);
    m_impl->zValueLabel = new QLabel("1", m_impl->zRow);

    m_impl->zLabel->setStyleSheet("QLabel { color: #CCCCCC; font-size: 11px; }");
    m_impl->zLabel->setFixedWidth(40);
    m_impl->zValueLabel->setStyleSheet("QLabel { color: #CCCCCC; font-size: 11px; }");
    m_impl->zValueLabel->setFixedWidth(30);
    m_impl->zValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_impl->zSlider->setFixedWidth(120);
    m_impl->zSlider->setMinimum(1);
    m_impl->zSlider->setMaximum(1);
    m_impl->zSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #CCC; width: 12px; margin: -4px 0; border-radius: 6px; }");

    auto* zLayout = new QHBoxLayout(m_impl->zRow);
    zLayout->setContentsMargins(0, 0, 0, 0);
    zLayout->setSpacing(4);
    zLayout->addWidget(m_impl->zLabel);
    zLayout->addWidget(m_impl->zSlider);
    zLayout->addWidget(m_impl->zValueLabel);

    // T row
    m_impl->tRow = new QWidget(bg);
    m_impl->tLabel = new QLabel("T (1)", m_impl->tRow);
    m_impl->tSlider = new QSlider(Qt::Horizontal, m_impl->tRow);
    m_impl->tValueLabel = new QLabel("1", m_impl->tRow);

    m_impl->tLabel->setStyleSheet("QLabel { color: #CCCCCC; font-size: 11px; }");
    m_impl->tLabel->setFixedWidth(40);
    m_impl->tValueLabel->setStyleSheet("QLabel { color: #CCCCCC; font-size: 11px; }");
    m_impl->tValueLabel->setFixedWidth(30);
    m_impl->tValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_impl->tSlider->setFixedWidth(120);
    m_impl->tSlider->setMinimum(1);
    m_impl->tSlider->setMaximum(1);
    m_impl->tSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #CCC; width: 12px; margin: -4px 0; border-radius: 6px; }");

    auto* tLayout = new QHBoxLayout(m_impl->tRow);
    tLayout->setContentsMargins(0, 0, 0, 0);
    tLayout->setSpacing(4);
    tLayout->addWidget(m_impl->tLabel);
    tLayout->addWidget(m_impl->tSlider);
    tLayout->addWidget(m_impl->tValueLabel);

    // Background layout
    auto* bgLayout = new QVBoxLayout(bg);
    bgLayout->setContentsMargins(8, 6, 8, 6);
    bgLayout->setSpacing(4);
    bgLayout->addWidget(m_impl->zRow);
    bgLayout->addWidget(m_impl->tRow);

    // Main layout
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(bg);

    // Connections
    connect(m_impl->zSlider, &QSlider::valueChanged, this, [this](int value) {
        m_impl->zValueLabel->setText(QString::number(value));
        if (!m_impl->updatingValues) {
            emit zSliceChanged(value - 1);
        }
    });

    connect(m_impl->tSlider, &QSlider::valueChanged, this, [this](int value) {
        m_impl->tValueLabel->setText(QString::number(value));
        if (!m_impl->updatingValues) {
            emit tFrameChanged(value - 1);
        }
    });

    // Initialize hidden
    setSliceFrameCounts(1, 1);
}

ZTNavigationWidget::~ZTNavigationWidget() = default;

void ZTNavigationWidget::setSliceFrameCounts(int numZSlices, int numTFrames)
{
    m_impl->numZSlices = numZSlices;
    m_impl->numTFrames = numTFrames;

    m_impl->zRow->setVisible(numZSlices > 1);
    m_impl->tRow->setVisible(numTFrames > 1);

    if (numZSlices <= 1 && numTFrames <= 1) {
        hide();
    } else {
        show();
    }

    m_impl->zLabel->setText(QString("Z (%1)").arg(numZSlices));
    m_impl->tLabel->setText(QString("T (%1)").arg(numTFrames));

    m_impl->updatingValues = true;
    m_impl->zSlider->setRange(1, numZSlices);
    m_impl->zSlider->setValue(1);
    m_impl->zValueLabel->setText("1");
    m_impl->tSlider->setRange(1, numTFrames);
    m_impl->tSlider->setValue(1);
    m_impl->tValueLabel->setText("1");
    m_impl->updatingValues = false;
}

void ZTNavigationWidget::setCurrentValues(int zIndex, int tFrame)
{
    m_impl->updatingValues = true;
    m_impl->zSlider->setValue(zIndex + 1);
    m_impl->zValueLabel->setText(QString::number(zIndex + 1));
    m_impl->tSlider->setValue(tFrame + 1);
    m_impl->tValueLabel->setText(QString::number(tFrame + 1));
    m_impl->updatingValues = false;
}

} // namespace slideio::viewer::ui
