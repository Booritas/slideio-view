#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

namespace slideio::viewer::ui
{

// Top-level resizable window that paints a single QImage scaled to fit the
// widget's current size, preserving aspect ratio. Used for showing a slide's
// associated images (label, macro, preview, …) when the user clicks one of
// the thumbnails in the Associated Images panel. Each click spawns a new
// instance; the window auto-deletes on close.
class AssociatedImageWindow : public QWidget
{
    Q_OBJECT

public:
    AssociatedImageWindow(const QString& title, const QImage& image,
                          QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
};

} // namespace slideio::viewer::ui
