#include "slideio/viewer/ui/StatusBarManager.h"

#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>

#include <cmath>

namespace slideio::viewer::ui
{

namespace
{
constexpr int kTransientMessageTimeoutMs = 4000;
}

StatusBarManager::StatusBarManager(QObject* parent)
    : QObject(parent)
    , m_statusBar(nullptr)
    , m_cursorLabel(nullptr)
    , m_magnificationLabel(nullptr)
    , m_annotationsLabel(nullptr)
    , m_colorLabel(nullptr)
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

    // Amber rather than the bar's uniform #CCCCCC: this reports a state the
    // user is blocked by, not a readout like the cursor position beside it.
    m_annotationsLabel = new QLabel(QString(), m_statusBar);
    m_annotationsLabel->setStyleSheet("QLabel { color: #D9A441; padding: 0 8px; }");
    m_annotationsLabel->setVisible(false);
    m_statusBar->addPermanentWidget(m_annotationsLabel);

    m_colorLabel = new QLabel(QString(), m_statusBar);
    m_colorLabel->setVisible(false);
    m_statusBar->addPermanentWidget(m_colorLabel);

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

void StatusBarManager::setLoading(bool loading)
{
    if (m_loadingIndicator) {
        m_loadingIndicator->setVisible(loading);
    }
}

void StatusBarManager::setColorManaged(bool managed)
{
    if (!m_colorLabel) {
        return;
    }
    m_colorLabel->setText(managed ? QStringLiteral("sRGB") : QString());
    m_colorLabel->setVisible(managed);
}

void StatusBarManager::setAnnotationsBlocked(bool blocked, const QString& reason)
{
    if (!m_annotationsLabel) {
        return;
    }
    m_annotationsLabel->setText(blocked ? tr("Annotations disabled") : QString());
    // The reason is the only place the cause is stated once the transient
    // message has expired, so it is cleared with the label rather than left
    // behind to describe a slide that is no longer open.
    m_annotationsLabel->setToolTip(blocked ? reason : QString());
    m_annotationsLabel->setVisible(blocked);
}

void StatusBarManager::showTransientMessage(const QString& text)
{
    if (m_statusBar) {
        m_statusBar->showMessage(text, kTransientMessageTimeoutMs);
    }
}

} // namespace slideio::viewer::ui
