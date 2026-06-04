#include "slideio/viewer/ui/ReadingSettings.h"

#include <QSettings>
#include <QString>

#include <algorithm>

namespace
{
const QString kThreadPoolSizeKey = "reading/threadPoolSize";
} // anonymous namespace

namespace slideio::viewer::ui
{

int clampThreadPoolSize(int n)
{
    return std::clamp(n, kMinThreadPoolSize, kMaxThreadPoolSize);
}

int readThreadPoolSize()
{
    QSettings settings;
    if (!settings.contains(kThreadPoolSizeKey)) {
        return kDefaultThreadPoolSize;
    }
    return clampThreadPoolSize(settings.value(kThreadPoolSizeKey).toInt());
}

void saveThreadPoolSize(int n)
{
    QSettings settings;
    settings.setValue(kThreadPoolSizeKey, clampThreadPoolSize(n));
}

} // namespace slideio::viewer::ui
