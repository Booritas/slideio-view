#include "slideio/viewer/core/AutosavePolicy.h"

namespace slideio::viewer::core
{

bool shouldAutosave(bool dirty,
                    std::chrono::steady_clock::time_point lastMutation,
                    std::chrono::steady_clock::time_point lastSave,
                    std::chrono::steady_clock::time_point now,
                    std::chrono::milliseconds debounce,
                    std::chrono::milliseconds backstop)
{
    if (!dirty) {
        return false;
    }
    if (now - lastMutation >= debounce) {
        return true;
    }
    return now - lastSave >= backstop;
}

} // namespace slideio::viewer::core
