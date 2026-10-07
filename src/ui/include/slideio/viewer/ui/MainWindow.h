#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>
#include <string>

namespace slideio::viewer::ui
{

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void openSlide(const std::string& path, const std::string& driverId = "");

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onSetDefaultColorProfile();
    void onClearDefaultColorProfile();
    void onColorManagementToggled(bool enabled);

    // Reads the configured default ICC profile (if any) from disk and hands
    // its bytes to the viewport. Called once at startup and again whenever
    // the setting changes.
    void applyDefaultColorProfile();

    // Tells the user that a just-changed default profile only affects slides
    // opened from now on. Does nothing when no slide is open, since there is
    // then nothing for the change not to apply to.
    void warnDefaultProfileAppliesToNewSlides();

    void onSetSlideColorProfile();
    void onClearSlideColorProfile();

    // Opens a file picker (captioned `dialogCaption`) for an ICC profile and
    // validates it -- unreadable, structurally implausible, or non-RGB are all
    // rejected with a QMessageBox titled `messageBoxTitle` naming the specific
    // reason -- returning the chosen path, or std::nullopt when the user
    // cancelled or the file was rejected (the message having already been
    // shown). The two titles are separate because the file picker's caption
    // names the action ("Select Default ICC Profile") while the message boxes
    // name the subject ("Default ICC Profile"); that is the only textual
    // difference between the default-profile and per-slide-profile call sites,
    // which this factors out so it cannot not drift between them. Ends at
    // validation: what each caller does with the path next (write a setting;
    // go on to the embedded-profile warning and the store) differs enough to
    // stay with them.
    std::optional<QString> pickAndValidateIccProfile(const QString& dialogCaption,
                                                      const QString& messageBoxTitle);

    // Rebuilds the whole profile policy -- the validated default plus every
    // stored override -- and hands it to the viewport. Called at startup and
    // whenever either half changes.
    void applyColorProfilePolicy();

    // Identity of the slide on screen, or empty when none is open or its
    // scenes could not be enumerated.
    std::string currentSlideId() const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
