#pragma once

#include "slideio/viewer/core/ISlideSource.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace slideio::viewer::infra
{

class SlideIOAdapterPool
{
public:
    class AdapterLoan
    {
    public:
        AdapterLoan();
        AdapterLoan(std::unique_ptr<core::ISlideSource> adapter, SlideIOAdapterPool* pool);
        ~AdapterLoan();

        AdapterLoan(const AdapterLoan&) = delete;
        AdapterLoan& operator=(const AdapterLoan&) = delete;

        AdapterLoan(AdapterLoan&& other) noexcept;
        AdapterLoan& operator=(AdapterLoan&& other) noexcept;

        core::ISlideSource* operator->() const;
        core::ISlideSource& operator*() const;
        explicit operator bool() const;

    private:
        std::unique_ptr<core::ISlideSource> m_adapter;
        SlideIOAdapterPool* m_pool;
    };

    explicit SlideIOAdapterPool(const std::string& filePath, int poolSize = 4);
    ~SlideIOAdapterPool();

    SlideIOAdapterPool(const SlideIOAdapterPool&) = delete;
    SlideIOAdapterPool& operator=(const SlideIOAdapterPool&) = delete;

    AdapterLoan acquire();
    int poolSize() const;

private:
    friend class AdapterLoan;
    void returnAdapter(std::unique_ptr<core::ISlideSource> adapter);

    std::string m_filePath;
    int m_poolSize;
    std::vector<std::unique_ptr<core::ISlideSource>> m_available;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
};

} // namespace slideio::viewer::infra
