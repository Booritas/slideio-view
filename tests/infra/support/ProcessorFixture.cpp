#include "ProcessorFixture.h"

#include <slideio/processor/config/configuration.hpp>
#include <slideio/processor/core/geometry.hpp>
#include <slideio/processor/core/ids.hpp>
#include <slideio/processor/core/measurement.hpp>
#include <slideio/processor/core/object_record.hpp>
#include <slideio/processor/core/provenance.hpp>
#include <slideio/processor/storage/annotation_store.hpp>
#include <slideio/processor/storage/layer_registry.hpp>
#include <slideio/processor/storage/object_store.hpp>
#include <slideio/processor/storage/stage_state_writer.hpp>
#include <slideio/processor/storage/store.hpp>
#include <slideio/processor/storage/types.hpp>
#include <slideio/processor/storage/vocabulary_registry.hpp>

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace slideio::viewer::test
{

namespace sp = slideio::processor;

namespace
{

sp::Polygon square(int64_t x, int64_t y, int64_t side)
{
    const sp::Point64 points[] = {{x, y}, {x + side, y}, {x + side, y + side}, {x, y + side}};
    sp::Polygon polygon;
    polygon.addRing(points, 4, false);
    return polygon;
}

std::string idText(const sp::ObjectId& id)
{
    std::vector<char> buffer(static_cast<size_t>(id.toString(nullptr, 0)) + 1);
    id.toString(buffer.data(), static_cast<int32_t>(buffer.size()));
    return std::string(buffer.data());
}

sp::LabelId findLabel(sp::Store& store, const char* name)
{
    sp::LabelId label;
    if (!store.vocabulary().find(name, label)) {
        throw std::runtime_error(std::string("fixture: no standard label ") + name);
    }
    return label;
}

} // namespace

struct ProcessorFixture::Impl
{
    sp::StorePtr store;
    sp::LayerId tissue;
    sp::LayerId nuclei;
    sp::ObjectId tissueId;
    sp::ObjectId tumourNucleus;
    sp::ObjectId stromaNucleus;
    int extraStages = 0;
};

ProcessorFixture::ProcessorFixture(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

ProcessorFixture::~ProcessorFixture()
{
    if (m_impl && m_impl->store) {
        m_impl->store->close();
    }
}

std::unique_ptr<ProcessorFixture> ProcessorFixture::create(const std::string& dir)
{
    auto impl = std::make_unique<Impl>();
    const std::filesystem::path path = std::filesystem::u8path(dir) / "workspace.store";
    impl->store = sp::Store::open(path.u8string().c_str(), sp::OpenMode::CreateNew, sp::Configuration());
    sp::Store& store = *impl->store;

    sp::Provenance provenance = store.provenance();
    provenance.setSlideId(kSlidePath);
    provenance.setSceneIndex(kScene);
    store.setProvenance(provenance);

    sp::LayerRegistry& layers = store.layers();
    impl->tissue = layers.declareSemantic("tissue", layers.pixelLayer());
    impl->nuclei = layers.declareSemantic("nuclei", impl->tissue);

    sp::StageStateWriter& stages = store.stageStateWriter();
    const sp::StageId segmentTissue = stages.declare("segment-tissue", "{}");
    const sp::StageId segmentNuclei = stages.declare("segment-nuclei", "{}");
    const sp::StageId classify = stages.declare(kClassifyStage, "{}");
    stages.addWrittenLayer(segmentTissue, impl->tissue);
    stages.addWrittenLayer(segmentNuclei, impl->nuclei);

    const sp::LabelId tumour = findLabel(store, "tumour");
    const sp::LabelId stroma = findLabel(store, "stroma");

    // Tissue: a 0..1000 square with a 400..600 hole.
    {
        sp::Polygon outline = square(0, 0, 1000);
        const sp::Point64 hole[] = {{400, 400}, {600, 400}, {600, 600}, {400, 600}};
        outline.addRing(hole, 4, true);
        sp::ObjectRecord record;
        record.setLayer(impl->tissue);
        record.setGeometry(outline);
        sp::ObjectWriterPtr writer = store.objects().writerForStage(segmentTissue);
        writer->emit(record);
        writer->flush();
        impl->tissueId = record.id();
        stages.setStatus(segmentTissue, sp::StageStatus::Complete);
    }

    // Nuclei: 3 rows of 10, at x = 100 + 80c and y = 100, 300, 700 (clear of the hole).
    const int64_t rowY[] = {100, 300, 700};
    std::vector<sp::ObjectId> rowStroma;
    {
        sp::ObjectWriterPtr writer = store.objects().writerForStage(segmentNuclei);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 10; ++c) {
                sp::ObjectRecord record;
                record.setLayer(impl->nuclei);
                record.setGeometry(square(100 + 80 * c, rowY[r], 10));
                if (r == 0) {
                    record.setLabel(tumour);
                }
                record.measurements().set("area", 100.0, sp::MeasurementUnit::SquarePixels);
                writer->emit(record);
                if (r == 0 && c == 0) {
                    impl->tumourNucleus = record.id();
                }
                if (r == 1) {
                    rowStroma.push_back(record.id());
                }
            }
        }
        writer->flush();
        stages.setStatus(segmentNuclei, sp::StageStatus::Complete);
    }
    impl->stromaNucleus = rowStroma.front();

    // A second stage labels row 1 stroma and measures every nucleus.
    {
        sp::AnnotationWriterPtr writer = store.annotations().writerForStage(classify);
        for (const sp::ObjectId& id : rowStroma) {
            writer->assertLabel(id, stroma);
        }
        sp::ObjectQuery all;
        all.layer(impl->nuclei);
        sp::CursorPtr cursor = store.objects().query(all);
        std::vector<sp::ObjectId> ids;
        while (cursor->next()) {
            ids.push_back(cursor->current().id());
        }
        cursor->close();
        for (const sp::ObjectId& id : ids) {
            writer->assertMeasurement(id, "intensity.mean", 128.0, sp::MeasurementUnit::Intensity);
        }
        writer->flush();
        stages.setStatus(classify, sp::StageStatus::Complete);
    }

    return std::unique_ptr<ProcessorFixture>(new ProcessorFixture(std::move(impl)));
}

std::string ProcessorFixture::tissueId() const
{
    return idText(m_impl->tissueId);
}

std::string ProcessorFixture::tumourNucleusId() const
{
    return idText(m_impl->tumourNucleus);
}

std::string ProcessorFixture::stromaNucleusId() const
{
    return idText(m_impl->stromaNucleus);
}

void ProcessorFixture::addNucleus(long long x, long long y)
{
    sp::Store& store = *m_impl->store;
    sp::StageStateWriter& stages = store.stageStateWriter();
    const std::string name = "segment-nuclei-extra-" + std::to_string(++m_impl->extraStages);
    const sp::StageId stage = stages.declare(name.c_str(), "{}");
    stages.addWrittenLayer(stage, m_impl->nuclei);
    sp::ObjectRecord record;
    record.setLayer(m_impl->nuclei);
    record.setGeometry(square(x, y, 10));
    record.measurements().set("area", 100.0, sp::MeasurementUnit::SquarePixels);
    sp::ObjectWriterPtr writer = store.objects().writerForStage(stage);
    writer->emit(record);
    writer->flush();
    stages.setStatus(stage, sp::StageStatus::Complete);
}

} // namespace slideio::viewer::test
