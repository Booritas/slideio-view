#include "slideio/viewer/ui/StatusBarManager.h"

#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>

#include <cmath>

namespace slideio::viewer::ui
{

StatusBarManager::StatusBarManager(QObject* parent)
    : QObject(parent)
    , m_statusBar(nullptr)
    , m_cursorLabel(nullptr)
    , m_magnificationLabel(nullptr)
    , m_scaleBarLabel(nullptr)
    , m_loadingIndicator(nullptr)
{
}

StatusBarManager::~StatusBarManager() = default;

void StatusBarManager::setup(QStatusBar* statusBar)
{
    m_statusBar = statusBar;
    if (!m_statusBar) {
        return;
    }

    m_statusBar->setStyleSheet(
        "QStatusBar { background: #2D2D2D; color: #CCCCCC; }"
        "QStatusBar::item { border: none; }"
        "QLabel { color: #CCCCCC; padding: 0 8px; }");

    m_cursorLabel = new QLabel("X: 0  Y: 0", m_statusBar);
    m_cursorLabel->setMinimumWidth(160);
    m_statusBar->addPermanentWidget(m_cursorLabel);

    m_magnificationLabel = new QLabel("100%", m_statusBar);
    m_magnificationLabel->setMinimumWidth(100);
    m_statusBar->addPermanentWidget(m_magnificationLabel);

    m_scaleBarLabel = new QLabel("", m_statusBar);
    m_scaleBarLabel->setMinimumWidth(120);
    m_statusBar->addPermanentWidget(m_scaleBarLabel);

    m_loadingIndicator = new QProgressBar(m_statusBar);
    m_loadingIndicator->setMaximumWidth(100);
    m_loadingIndicator->setMaximumHeight(16);
    m_loadingIndicator->setRange(0, 0); // indeterminate
    m_loadingIndicator->setTextVisible(false);
    m_loadingIndicator->setStyleSheet(
        "QProgressBar { border: 1px solid #555555; background: #333333; }"
        "QProgressBar::chunk { background: #4A90D9; }");
    m_loadingIndicator->setVisible(false);
    m_statusBar->addPermanentWidget(m_loadingIndicator);
}

void StatusBarManager::updateCursorPosition(double slideX, double slideY)
{
    if (m_cursorLabel) {
        m_cursorLabel->setText(
            QString("X: %1  Y: %2")
                .arg(static_cast<int>(std::round(slideX)))
                .arg(static_cast<int>(std::round(slideY))));
    }
}

void StatusBarManager::updateMagnification(double scale, double baseMagnification)
{
    if (!m_magnificationLabel) {
        return;
    }

    double percentage = scale * 100.0;

    if (baseMagnification > 0.0) {
        double magnification = scale * baseMagnification;
        m_magnificationLabel->setText(
            QString("%1x (%2%)")
                .arg(magnification, 0, 'f', 1)
                .arg(percentage, 0, 'f', 1));
    } else {
        m_magnificationLabel->setText(
            QString("%1%").arg(percentage, 0, 'f', 1));
    }
}

void StatusBarManager::updateScaleBar(double resolutionMPP)
{
    if (!m_scaleBarLabel) {
        return;
    }

    if (resolutionMPP <= 0.0) {
        m_scaleBarLabel->setText("");
        return;
    }

    // Choose a human-friendly scale bar length
    // resolutionMPP is micrometers per pixel at the current view scale
    // We want to display a scale bar that represents a round number of micrometers or millimeters

    static const double kScaleBarLengths[] = {
        1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0,
        1000.0, 2000.0, 5000.0, 10000.0
    };

    // Target ~100 pixels for the scale bar
    double targetMicrons = resolutionMPP * 100.0;

    double bestLength = kScaleBarLengths[0];
    double bestDiff = std::abs(targetMicrons - bestLength);

    for (double len : kScaleBarLengths) {
        double diff = std::abs(targetMicrons - len);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestLength = len;
        }
    }

    QString text;
    if (bestLength >= 1000.0) {
        text = QString("%1 mm").arg(bestLength / 1000.0, 0, 'f', 1);
    } else {
        text = QString::fromUtf8("%1 \xC2\xB5m").arg(bestLength, 0, 'f', 0);
    }

    m_scaleBarLabel->setText(text);
}

void StatusBarManager::setLoading(bool loading)
{
    if (m_loadingIndicator) {
        m_loadingIndicator->setVisible(loading);
    }
}

} // namespace slideio::viewer::ui
