#pragma once

#include <QString>
#include <QWidget>

class QTimer;

namespace slideio::viewer::ui
{

class LoadingOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingOverlay(QWidget* parent = nullptr);
    ~LoadingOverlay() override;

    LoadingOverlay(const LoadingOverlay&) = delete;
    LoadingOverlay& operator=(const LoadingOverlay&) = delete;

    void start(const QString& filename);
    void stop();
    void setStatus(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QString m_filename;
    QString m_status;
    int m_angle = 0;
    QTimer* m_timer = nullptr;
};

} // namespace slideio::viewer::ui
