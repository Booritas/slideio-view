#pragma once

#include <chrono>

namespace slideio::viewer::core
{

/// Quiet period after the last edit before a save is worth making.
inline constexpr std::chrono::milliseconds kAutosaveDebounce{2000};

/// Longest a dirty document may go unwritten while editing continues. Without
/// it, continuous editing resets the debounce forever and nothing is saved.
inline constexpr std::chrono::milliseconds kAutosaveBackstop{30000};

/// True when a dirty document should be written now.
///
/// Pure so it can be tested: no test suite creates a QCoreApplication, so a
/// QTimer never fires in a test. The timer calls this; the decision lives here.
bool shouldAutosave(bool dirty,
                    std::chrono::steady_clock::time_point lastMutation,
                    std::chrono::steady_clock::time_point lastSave,
                    std::chrono::steady_clock::time_point now,
                    std::chrono::milliseconds debounce,
                    std::chrono::milliseconds backstop);

} // namespace slideio::viewer::core
