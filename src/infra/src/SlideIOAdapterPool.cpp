#include "slideio/viewer/infra/SlideIOAdapterPool.h"
#include "slideio/viewer/infra/SlideIOAdapter.h"

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

namespace slideio::viewer::infra
{

// -- AdapterLoan implementation --

SlideIOAdapterPool::AdapterLoan::AdapterLoan()
    : m_adapter(nullptr)
    , m_pool(nullptr)
{
}

SlideIOAdapterPool::AdapterLoan::AdapterLoan(std::unique_ptr<core::ISlideSource> adapter,
                                              SlideIOAdapterPool* pool)
    : m_adapter(std::move(adapter))
    , m_pool(pool)
{
}

SlideIOAdapterPool::AdapterLoan::~AdapterLoan()
{
    if (m_adapter && m_pool) {
        m_pool->returnAdapter(std::move(m_adapter));
    }
}

SlideIOAdapterPool::AdapterLoan::AdapterLoan(AdapterLoan&& other) noexcept
    : m_adapter(std::move(other.m_adapter))
    , m_pool(other.m_pool)
{
    other.m_pool = nullptr;
}

SlideIOAdapterPool::AdapterLoan& SlideIOAdapterPool::AdapterLoan::operator=(AdapterLoan&& other) noexcept
{
    if (this != &other) {
        if (m_adapter && m_pool) {
            m_pool->returnAdapter(std::move(m_adapter));
        }
        m_adapter = std::move(other.m_adapter);
        m_pool = other.m_pool;
        other.m_pool = nullptr;
    }
    return *this;
}

core::ISlideSource* SlideIOAdapterPool::AdapterLoan::operator->() const
{
    return m_adapter.get();
}

core::ISlideSource& SlideIOAdapterPool::AdapterLoan::operator*() const
{
    return *m_adapter;
}

SlideIOAdapterPool::AdapterLoan::operator bool() const
{
    return m_adapter != nullptr;
}

// -- SlideIOAdapterPool implementation --

SlideIOAdapterPool::SlideIOAdapterPool(const std::string& filePath, int poolSize)
    : m_filePath(filePath)
    , m_poolSize(poolSize)
{
    if (poolSize <= 0) {
        throw std::invalid_argument("SlideIOAdapterPool: poolSize must be > 0");
    }

    spdlog::info("SlideIOAdapterPool: creating pool of {} adapters for '{}'", poolSize, filePath);

    m_available.reserve(static_cast<size_t>(poolSize));
    for (int i = 0; i < poolSize; ++i) {
        m_available.push_back(std::make_unique<SlideIOAdapter>(filePath));
    }

    spdlog::info("SlideIOAdapterPool: pool created successfully");
}

SlideIOAdapterPool::~SlideIOAdapterPool()
{
    spdlog::debug("SlideIOAdapterPool: destroying pool for '{}'", m_filePath);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_available.clear();
}

SlideIOAdapterPool::AdapterLoan SlideIOAdapterPool::acquire()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condition.wait(lock, [this] { return !m_available.empty(); });

    auto adapter = std::move(m_available.back());
    m_available.pop_back();

    spdlog::debug("SlideIOAdapterPool: adapter acquired, {} remaining", m_available.size());

    return AdapterLoan(std::move(adapter), this);
}

int SlideIOAdapterPool::poolSize() const
{
    return m_poolSize;
}

void SlideIOAdapterPool::returnAdapter(std::unique_ptr<core::ISlideSource> adapter)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_available.push_back(std::move(adapter));
        spdlog::debug("SlideIOAdapterPool: adapter returned, {} available", m_available.size());
    }
    m_condition.notify_one();
}

} // namespace slideio::viewer::infra
