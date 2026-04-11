#pragma once

#include "slideio/viewer/core/Types.h"

#include <QDockWidget>
#include <memory>
#include <vector>

namespace slideio::viewer::ui
{

class ChannelMixerPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit ChannelMixerPanel(QWidget* parent = nullptr);
    ~ChannelMixerPanel() override;

    void setChannels(const std::vector<core::ChannelInfo>& channels);
    void clearChannels();
    std::vector<core::ChannelInfo> channelSettings() const;

signals:
    void channelSettingsChanged(const std::vector<core::ChannelInfo>& channels);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
