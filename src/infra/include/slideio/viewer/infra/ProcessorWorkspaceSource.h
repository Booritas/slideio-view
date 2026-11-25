#pragma once

#include "slideio/viewer/core/ResultsSource.h"

#include <memory>
#include <string>

namespace slideio::viewer::infra
{

/// Reads a slideio-processor workspace store, `<dir>/workspace.store`, read-only.
///
/// The layer list, each layer's classes (legend order), unclassified count and
/// stage state are read once, at open(), and shared by sibling(); class indices
/// in ResultObject refer to that list. Queries and details read the store
/// afresh, so objects committed by a writer after open() are seen.
///
/// Not thread-safe: use one instance per thread.
class ProcessorWorkspaceSource : public core::ResultsSource
{
public:
    /// Throws core::ResultsError naming the folder when it holds no
    /// workspace.store or the store fails to open.
    static std::unique_ptr<ProcessorWorkspaceSource> open(const std::string& workspaceDirectory);

    ~ProcessorWorkspaceSource() override;
    ProcessorWorkspaceSource(const ProcessorWorkspaceSource&) = delete;
    ProcessorWorkspaceSource& operator=(const ProcessorWorkspaceSource&) = delete;

    core::ResultsProvenance provenance() const override;
    std::vector<core::ResultLayerInfo> layers() const override;
    core::ResultQueryAnswer query(int layer, const core::Rect<double>& rect, int64_t budget) override;
    std::optional<core::ResultDetails> details(const std::string& objectId) override;
    void scan(int layer,
              const std::function<void(const core::Rect<double>&, const std::vector<int>&)>& visit,
              const std::function<bool(double)>& progress) override;
    /// Opens the same store again, sharing this source's layer list and class
    /// mapping: no provenance read and no per-label counting, so it is cheap.
    std::function<std::unique_ptr<core::ResultsSource>()> sibling() const override;

private:
    struct Impl;
    explicit ProcessorWorkspaceSource(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::infra
