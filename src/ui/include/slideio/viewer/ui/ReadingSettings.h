#pragma once

namespace slideio::viewer::ui
{

// Image-reading thread-pool size bounds and default.
inline constexpr int kDefaultThreadPoolSize = 4;
inline constexpr int kMinThreadPoolSize = 1;
inline constexpr int kMaxThreadPoolSize = 32;

// Clamps a requested thread-pool size to [kMinThreadPoolSize, kMaxThreadPoolSize].
int clampThreadPoolSize(int n);

// Reads the persisted thread-pool size (QSettings key "reading/threadPoolSize"),
// clamped; returns kDefaultThreadPoolSize when unset.
int readThreadPoolSize();

// Persists the thread-pool size (clamped) to QSettings.
void saveThreadPoolSize(int n);

} // namespace slideio::viewer::ui
