#pragma once

namespace spdlog
{
class logger;
}

namespace slideio::viewer::infra
{

// Returns the process-wide "perf" logger used for tile-loading and rendering
// timing. Never null: when no "perf" logger is registered (e.g. unit tests or
// before main() sets one up), a static no-op logger at level::off is returned,
// so callers never have to branch on null.
spdlog::logger& perfLog();

// True when the perf logger would emit a trace-level record. Use to guard the
// construction of expensive log arguments (e.g. TileKey::toString()) so there
// is no cost when perf logging is disabled (the default).
bool perfLogEnabled();

} // namespace slideio::viewer::infra
