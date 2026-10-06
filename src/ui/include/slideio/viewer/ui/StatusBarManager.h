#pragma once

#include <QObject>

class QLabel;
class QProgressBar;
class QStatusBar;

namespace slideio::viewer::ui
{

class StatusBarManager : public QObject
{
    Q_OBJECT

public:
    explicit StatusBarManager(QObject* parent = nullptr);
    ~StatusBarManager() override;

    void setup(QStatusBar* statusBar);

    void updateCursorPosition(double slideX, double slideY);
    void updateMagnification(double scale, double baseMagnification);
    void setLoading(bool loading);
    void setColorManaged(bool managed);

private:
    QStatusBar* m_statusBar;
    QLabel* m_cursorLabel;
    QLabel* m_magnificationLabel;
    QLabel* m_colorLabel;
    QProgressBar* m_loadingIndicator;
};

} // namespace slideio::viewer::ui
