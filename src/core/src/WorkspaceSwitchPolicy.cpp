#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

namespace slideio::viewer::core
{

WorkspaceSwitchOptions workspaceSwitchOptions(const WorkspaceSwitchFacts& facts)
{
    WorkspaceSwitchOptions options;

    if (facts.currentCount == 0) {
        // Nothing on screen to lose. The new folder's annotations, if any, just
        // load.
        return options;
    }

    options.ask = true;
    options.loadNew = true;

    if (!facts.currentWritable || !facts.newFileReadable) {
        return options;
    }

    if (facts.newFileExists) {
        options.keepCurrent = true;
        options.merge = true;
    } else {
        options.copy = true;
    }

    return options;
}

} // namespace slideio::viewer::core
