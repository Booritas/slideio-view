#pragma once

#include <QWidget>

#include <memory>

namespace slideio::viewer::ui
{

// The "Performance" tab of the Settings dialog: configures the image-reading
// thread-pool size. The chosen value is persisted by apply() and takes effect
// on the next opened slide.
class PerformanceTab : public QWidget
{
    Q_OBJECT

public:
    explicit PerformanceTab(QWidget* parent = nullptr);
    ~PerformanceTab() override;

    PerformanceTab(const PerformanceTab&) = delete;
    PerformanceTab& operator=(const PerformanceTab&) = delete;

    // Persist the current selection to QSettings.
    void apply();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
