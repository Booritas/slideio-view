#include "slideio/viewer/ui/LoadingOverlay.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QTimer>

namespace slideio::viewer::ui
{

namespace
{
constexpr int kSpinnerRadius = 28;
constexpr int kSpinnerThickness = 5;
constexpr int kSpinnerSweepDeg = 270;
constexpr int kRotationStepDeg = 12;
constexpr int kRotationIntervalMs = 33; // ~30 FPS
} // namespace

LoadingOverlay::LoadingOverlay(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    hide();

    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + kRotationStepDeg) % 360;
        update();
    });
}

LoadingOverlay::~LoadingOverlay() = default;

void LoadingOverlay::start(const QString& filename)
{
    m_filename = filename;
    m_status.clear();
    m_angle = 0;
    show();
    raise();
    setFocus();
    m_timer->start(kRotationIntervalMs);
    update();
}

void LoadingOverlay::stop()
{
    m_timer->stop();
    hide();
}

void LoadingOverlay::setStatus(const QString& text)
{
    m_status = text;
    update();
}

void LoadingOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Semi-transparent dark backdrop
    p.fillRect(rect(), QColor(0, 0, 0, 160));

    const int cx = width() / 2;
    const int cy = height() / 2;

    // Spinner: a rotating arc
    QPen pen(QColor(220, 220, 220));
    pen.setWidth(kSpinnerThickness);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    QRectF spinnerRect(cx - kSpinnerRadius, cy - kSpinnerRadius - 30,
                       kSpinnerRadius * 2, kSpinnerRadius * 2);
    // Qt drawArc: angles in 1/16th of a degree, counter-clockwise from 3 o'clock.
    p.drawArc(spinnerRect, -m_angle * 16, -kSpinnerSweepDeg * 16);

    // Filename / loading text
    p.setPen(QColor(230, 230, 230));
    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleFont.setBold(true);
    p.setFont(titleFont);

    QString primary = m_filename.isEmpty()
        ? QStringLiteral("Loading...")
        : QStringLiteral("Loading %1...").arg(m_filename);

    QFontMetrics fm(titleFont);
    int textY = cy + kSpinnerRadius - 10;
    int textWidth = fm.horizontalAdvance(primary);
    p.drawText(cx - textWidth / 2, textY, primary);

    // Optional sub-status
    if (!m_status.isEmpty()) {
        QFont statusFont = font();
        statusFont.setPointSizeF(statusFont.pointSizeF());
        p.setFont(statusFont);
        p.setPen(QColor(190, 190, 190));
        QFontMetrics sfm(statusFont);
        int sWidth = sfm.horizontalAdvance(m_status);
        p.drawText(cx - sWidth / 2, textY + sfm.height() + 6, m_status);
    }
}

void LoadingOverlay::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void LoadingOverlay::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}

void LoadingOverlay::keyPressEvent(QKeyEvent* event)
{
    event->accept();
}

} // namespace slideio::viewer::ui
