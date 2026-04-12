#pragma once

#include <QWidget>
#include <memory>

namespace slideio::viewer::ui
{

class ZTNavigationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZTNavigationWidget(QWidget* parent = nullptr);
    ~ZTNavigationWidget() override;

    void setSliceFrameCounts(int numZSlices, int numTFrames);
    void setCurrentValues(int zIndex, int tFrame);

signals:
    void zSliceChanged(int zIndex);   // 0-indexed
    void tFrameChanged(int tFrame);   // 0-indexed

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
