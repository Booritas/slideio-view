#include "slideio/viewer/ui/AssociatedImageWindow.h"

#include <QGuiApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>

namespace slideio::viewer::ui
{

namespace
{
constexpr int kMinSide = 200;
constexpr double kInitialScreenFraction = 0.5; // initial size, capped to 50% of screen
} // namespace

AssociatedImageWindow::AssociatedImageWindow(const QString& title, const QImage& image, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_image(image)
{
    setWindowTitle(title.isEmpty() ? QStringLiteral("Associated Image") : title);
    setAttribute(Qt::WA_DeleteOnClose);  // free this window when the user closes it
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(kMinSide, kMinSide);

    // Pick a sensible initial size: the source image dimensions, but capped
    // to a fraction of the screen so huge associated images don't open at
    // full bitmap size. Aspect ratio is preserved.
    QSize startSize = image.size();
    if (auto* screen = QGuiApplication::primaryScreen()) {
        QSize cap = screen->availableSize() * kInitialScreenFraction;
        if (startSize.width() > cap.width() || startSize.height() > cap.height()) {
            startSize.scale(cap, Qt::KeepAspectRatio);
        }
    }
    if (startSize.width() < kMinSide || startSize.height() < kMinSide) {
        startSize.scale(kMinSide, kMinSide, Qt::KeepAspectRatioByExpanding);
    }
    resize(startSize);
}

void AssociatedImageWindow::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());

    if (m_image.isNull()) return;

    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Fit the image into the widget area while preserving aspect ratio,
    // then center it. Re-fit happens automatically because Qt invokes
    // paintEvent on every resize.
    QSize fitted = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    int x = (width()  - fitted.width())  / 2;
    int y = (height() - fitted.height()) / 2;
    QRect target(QPoint(x, y), fitted);
    painter.drawImage(target, m_image);
}

} // namespace slideio::viewer::ui
