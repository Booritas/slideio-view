#pragma once

#include <memory>
#include <string>

namespace slideio::viewer::test
{

/// Builds a slideio-processor workspace store for the infra tests and keeps it
/// open ReadWrite (holding the writer lock) until destroyed.
///
/// Contents: slide path "/slides/fixture.svs", scene 1; semantic layers
/// "tissue" (one 0..1000 square with a 400..600 hole) and "nuclei" (30 squares
/// 10x10 on a 10x3 grid inside 0..1000). Row 0 carries the row label "tumour",
/// row 1 is labelled "stroma" by an assertion of the "classify-nuclei" stage,
/// row 2 is unlabelled. Every nucleus has its own "area" (square pixels) and an asserted
/// "intensity.mean" from "classify-nuclei".
class ProcessorFixture
{
public:
    static constexpr const char* kSlidePath = "/slides/fixture.svs";
    static constexpr int kScene = 1;
    static constexpr const char* kClassifyStage = "classify-nuclei";

    /// Creates `<dir>/workspace.store`; `dir` must exist and hold no store.
    static std::unique_ptr<ProcessorFixture> create(const std::string& dir);
    ~ProcessorFixture();
    ProcessorFixture(const ProcessorFixture&) = delete;
    ProcessorFixture& operator=(const ProcessorFixture&) = delete;

    /// ObjectId text of the tissue object, a tumour nucleus and a stroma nucleus.
    std::string tissueId() const;
    std::string tumourNucleusId() const;
    std::string stromaNucleusId() const;

    /// Writes one more 10x10 nucleus at (x, y) under a new completed stage.
    void addNucleus(long long x, long long y);

private:
    struct Impl;
    explicit ProcessorFixture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::test
