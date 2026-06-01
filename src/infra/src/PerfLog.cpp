#include "slideio/viewer/infra/PerfLog.h"

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

#include <memory>

namespace slideio::viewer::infra
{

spdlog::logger& perfLog()
{
    // Lazily-created no-op logger (no sinks, level off) used when the real
    // "perf" logger has not been registered.
    static std::shared_ptr<spdlog::logger> fallback = []
    {
        auto l = std::make_shared<spdlog::logger>("perf-null");
        l->set_level(spdlog::level::off);
        return l;
    }();

    if (auto registered = spdlog::get("perf"))
    {
        return *registered;
    }
    return *fallback;
}

bool perfLogEnabled()
{
    return perfLog().should_log(spdlog::level::trace);
}

} // namespace slideio::viewer::infra
