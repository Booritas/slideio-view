#pragma once

#include <QObject>
#include <QString>

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

    /// Shows a permanent indicator while annotations cannot be edited, with
    /// `reason` as its tooltip. Mirrors the drawing tools' enabled state, which
    /// on its own communicates nothing: a disabled menu item is rendered
    /// identically to an enabled one, and the message that explained the gate
    /// is transient. `reason` is ignored when `blocked` is false.
    void setAnnotationsBlocked(bool blocked, const QString& reason);

    /// Shows `text` in the status bar's left area for a few seconds. The
    /// permanent indicators are unaffected.
    void showTransientMessage(const QString& text);

private:
    QStatusBar* m_statusBar;
    QLabel* m_cursorLabel;
    QLabel* m_magnificationLabel;
    QLabel* m_annotationsLabel;
    QLabel* m_colorLabel;
    QProgressBar* m_loadingIndicator;
};

} // namespace slideio::viewer::ui
