#include "slideio/viewer/infra/ProcessorWorkspaceSource.h"

// The processor headers export classes holding std::unique_ptr pimpls, which
// MSVC flags (C4251) under this target's warning level; the library is built
// with the same toolchain, so the warning carries no risk here.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
#include <slideio/processor/config/configuration.hpp>
#include <slideio/processor/core/flow.hpp>
#include <slideio/processor/core/geometry.hpp>
#include <slideio/processor/core/ids.hpp>
#include <slideio/processor/core/measurement.hpp>
#include <slideio/processor/core/object_query.hpp>
#include <slideio/processor/core/object_record.hpp>
#include <slideio/processor/core/provenance.hpp>
#include <slideio/processor/storage/annotation_store.hpp>
#include <slideio/processor/storage/layer_registry.hpp>
#include <slideio/processor/storage/object_store.hpp>
#include <slideio/processor/storage/stage_state_reader.hpp>
#include <slideio/processor/storage/store.hpp>
#include <slideio/processor/storage/types.hpp>
#include <slideio/processor/storage/vocabulary_registry.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <map>
#include <system_error>
#include <utility>

namespace slideio::viewer::infra
{

namespace sp = slideio::processor;

namespace
{

/// Progress is reported before the first object and then every this many objects.
constexpr int64_t kScanProgressInterval = 1024;

std::string unitText(sp::MeasurementUnit unit)
{
    switch (unit) {
    case sp::MeasurementUnit::Pixels:
        return "px";
    case sp::MeasurementUnit::Micrometres:
        return "\xC2\xB5m"; // micrometre sign + m
    case sp::MeasurementUnit::SquarePixels:
        return "px\xC2\xB2"; // px + superscript two
    case sp::MeasurementUnit::SquareMicrometres:
        return "\xC2\xB5m\xC2\xB2";
    case sp::MeasurementUnit::Intensity:
    case sp::MeasurementUnit::Dimensionless:
        return "";
    }
    return "";
}

std::string idText(const sp::ObjectId& id)
{
    const int32_t length = id.toString(nullptr, 0);
    std::string text(static_cast<size_t>(length) + 1, '\0');
    id.toString(text.data(), static_cast<int32_t>(text.size()));
    text.resize(static_cast<size_t>(length));
    return text;
}

/// floor of the origin, ceil of the far corner.
sp::Rect64 toRect64(const core::Rect<double>& rect)
{
    const auto x0 = static_cast<int64_t>(std::floor(rect.x));
    const auto y0 = static_cast<int64_t>(std::floor(rect.y));
    const auto x1 = static_cast<int64_t>(std::ceil(rect.x + rect.width));
    const auto y1 = static_cast<int64_t>(std::ceil(rect.y + rect.height));
    return sp::Rect64{x0, y0, x1 - x0, y1 - y0};
}

core::Rect<double> toRect(const sp::Rect64& rect)
{
    return core::Rect<double>{static_cast<double>(rect.x), static_cast<double>(rect.y),
                              static_cast<double>(rect.width), static_cast<double>(rect.height)};
}

std::vector<core::ResultRing> toRings(const sp::Polygon& polygon)
{
    std::vector<core::ResultRing> rings;
    rings.reserve(static_cast<size_t>(polygon.ringCount()));
    std::vector<sp::Point64> points;
    for (int32_t r = 0; r < polygon.ringCount(); ++r) {
        const int64_t count = polygon.ringPointCount(r);
        points.resize(static_cast<size_t>(count));
        polygon.ringPoints(r, points.data(), count);
        core::ResultRing ring;
        ring.hole = polygon.ringIsHole(r);
        ring.points.reserve(points.size());
        for (const sp::Point64& p : points) {
            ring.points.push_back(core::PointF{static_cast<double>(p.x), static_cast<double>(p.y)});
        }
        rings.push_back(std::move(ring));
    }
    return rings;
}

/// Runs `body`, turning any library failure into a core::ResultsError.
template <typename Body>
auto guarded(const std::string& what, Body&& body) -> decltype(body())
{
    try {
        return body();
    } catch (const core::ResultsError&) {
        throw;
    } catch (const std::exception& e) {
        throw core::ResultsError(what + ": " + e.what());
    }
}

} // namespace

struct ProcessorWorkspaceSource::Impl
{
    struct Layer
    {
        sp::LayerId id;
        core::ResultLayerInfo info;
        std::map<int32_t, int> classIndex; ///< LabelId value -> index into info.classes
    };

    std::string directory;
    sp::StorePtr store;
    core::ResultsProvenance provenance;
    std::vector<Layer> layers;

    void readLayers();
    std::string stageStateOf(sp::LayerId layer) const;
    /// The record's effective label set: the row label first, then the asserted ones.
    std::vector<sp::LabelId> effectiveLabels(const sp::ObjectRecord& record, bool anyAssertions) const;
    std::vector<int> classesOf(const Layer& layer, const sp::ObjectRecord& record, bool anyAssertions) const;
    bool anyLabelAssertions() const { return store->annotations().labelAnnotationCount() > 0; }
    std::string stageName(sp::StageId stage) const;
    const Layer& layerAt(int layer) const;
};

void ProcessorWorkspaceSource::Impl::readLayers()
{
    const sp::LayerRegistry& registry = store->layers();
    const sp::VocabularyRegistry& vocabulary = store->vocabulary();
    sp::ObjectStore& objects = store->objects();
    const sp::LayerId pixel = registry.pixelLayer();

    for (int32_t i = 0; i < registry.count(); ++i) {
        const sp::LayerId id = registry.at(i);
        if (id == pixel || registry.isDropped(id)) {
            continue;
        }
        const int64_t objectCount = objects.objectCountInLayer(id);
        if (objectCount <= 0) {
            continue;
        }
        Layer layer;
        layer.id = id;
        layer.info.name = registry.name(id);
        layer.info.objectCount = objectCount;
        layer.info.stageState = stageStateOf(id);

        // The store keeps no per-layer label listing: count every vocabulary label
        // (the count matches the effective label set, asserted labels included).
        std::vector<std::pair<int32_t, core::ResultClassInfo>> classes;
        for (int32_t v = 0; v < vocabulary.count(); ++v) {
            const sp::LabelId label = vocabulary.at(v);
            sp::ObjectQuery query;
            query.layer(id).label(label);
            const int64_t count = objects.count(query);
            if (count > 0) {
                classes.push_back({label.value(), core::ResultClassInfo{vocabulary.name(label), count}});
            }
        }
        std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) {
            if (a.second.objectCount != b.second.objectCount) {
                return a.second.objectCount > b.second.objectCount;
            }
            return a.second.name < b.second.name;
        });
        for (auto& [labelValue, info] : classes) {
            layer.classIndex[labelValue] = static_cast<int>(layer.info.classes.size());
            layer.info.classes.push_back(std::move(info));
        }
        layers.push_back(std::move(layer));
    }
}

std::string ProcessorWorkspaceSource::Impl::stageStateOf(sp::LayerId layer) const
{
    const sp::LayerRegistry& registry = store->layers();
    sp::StageStateReader& stages = store->stageStates();
    bool stale = false;
    for (int32_t i = 0; i < registry.writerStageCount(layer); ++i) {
        const sp::StageId stage = registry.writerStageAt(layer, i);
        if (stages.status(stage) == sp::StageStatus::Failed) {
            return "failed";
        }
        stale = stale || stages.isStale(stage);
    }
    return stale ? "stale" : "";
}

std::vector<sp::LabelId> ProcessorWorkspaceSource::Impl::effectiveLabels(const sp::ObjectRecord& record,
                                                                          bool anyAssertions) const
{
    std::vector<sp::LabelId> labels;
    if (!anyAssertions) {
        // No stage asserted any label: the effective set is the row label alone.
        if (record.hasLabel()) {
            labels.push_back(record.label());
        }
        return labels;
    }
    const sp::AnnotationStore& annotations = store->annotations();
    const sp::ObjectId id = record.id();
    const int32_t count = annotations.labelCount(id);
    labels.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        labels.push_back(annotations.labelAt(id, i));
    }
    return labels;
}

std::vector<int> ProcessorWorkspaceSource::Impl::classesOf(const Layer& layer, const sp::ObjectRecord& record,
                                                           bool anyAssertions) const
{
    std::vector<int> classes;
    for (const sp::LabelId& label : effectiveLabels(record, anyAssertions)) {
        // A label first used after open() has no class in the legend read at open(); skip it.
        const auto found = layer.classIndex.find(label.value());
        if (found != layer.classIndex.end()) {
            classes.push_back(found->second);
        }
    }
    return classes;
}

std::string ProcessorWorkspaceSource::Impl::stageName(sp::StageId stage) const
{
    if (!stage.isValid()) {
        return "";
    }
    return store->stageStates().name(stage);
}

const ProcessorWorkspaceSource::Impl::Layer& ProcessorWorkspaceSource::Impl::layerAt(int layer) const
{
    if (layer < 0 || static_cast<size_t>(layer) >= layers.size()) {
        throw core::ResultsError("results layer index " + std::to_string(layer) + " out of range");
    }
    return layers[static_cast<size_t>(layer)];
}

ProcessorWorkspaceSource::ProcessorWorkspaceSource(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

ProcessorWorkspaceSource::~ProcessorWorkspaceSource() = default;

std::unique_ptr<ProcessorWorkspaceSource> ProcessorWorkspaceSource::open(const std::string& workspaceDirectory)
{
    const std::filesystem::path path = std::filesystem::u8path(workspaceDirectory) / "workspace.store";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        throw core::ResultsError("no workspace.store in " + workspaceDirectory);
    }

    auto impl = std::make_unique<Impl>();
    impl->directory = workspaceDirectory;
    try {
        // The viewer never writes: always ReadOnly, which takes no writer lock.
        impl->store = sp::Store::open(path.u8string().c_str(), sp::OpenMode::ReadOnly, sp::Configuration());
    } catch (const std::exception& e) {
        throw core::ResultsError("cannot open the results store in " + workspaceDirectory + ": " + e.what());
    }

    guarded("cannot read the results store in " + workspaceDirectory, [&] {
        const sp::Provenance provenance = impl->store->provenance();
        impl->provenance.slidePath = provenance.slideId();
        impl->provenance.scene = provenance.sceneIndex();
        impl->readLayers();
    });
    return std::unique_ptr<ProcessorWorkspaceSource>(new ProcessorWorkspaceSource(std::move(impl)));
}

core::ResultsProvenance ProcessorWorkspaceSource::provenance() const
{
    return m_impl->provenance;
}

std::vector<core::ResultLayerInfo> ProcessorWorkspaceSource::layers() const
{
    std::vector<core::ResultLayerInfo> result;
    result.reserve(m_impl->layers.size());
    for (const auto& layer : m_impl->layers) {
        result.push_back(layer.info);
    }
    return result;
}

core::ResultQueryAnswer ProcessorWorkspaceSource::query(int layer, const core::Rect<double>& rect, int64_t budget)
{
    const Impl::Layer& entry = m_impl->layerAt(layer);
    return guarded("cannot query the results store in " + m_impl->directory, [&] {
        sp::ObjectStore& objects = m_impl->store->objects();
        sp::ObjectQuery query;
        query.layer(entry.id).intersecting(toRect64(rect));

        core::ResultQueryAnswer answer;
        answer.inView = objects.count(query);
        if (answer.inView > budget) {
            answer.overBudget = true;
            return answer;
        }

        const bool anyAssertions = m_impl->anyLabelAssertions();
        sp::CursorPtr cursor = objects.query(query);
        answer.objects.reserve(static_cast<size_t>(answer.inView));
        while (cursor->next()) {
            const sp::ObjectRecord& record = cursor->current();
            core::ResultObject object;
            object.id = idText(record.id());
            object.layer = layer;
            object.classes = m_impl->classesOf(entry, record, anyAssertions);
            object.rings = toRings(record.geometry());
            object.bounds = toRect(record.boundingBox());
            answer.objects.push_back(std::move(object));
        }
        cursor->close();
        // A commit between the count and the cursor can add objects; report what was fetched.
        answer.inView = std::max<int64_t>(answer.inView, static_cast<int64_t>(answer.objects.size()));
        return answer;
    });
}

std::optional<core::ResultDetails> ProcessorWorkspaceSource::details(const std::string& objectId)
{
    sp::ObjectId id;
    if (!sp::ObjectId::fromString(objectId.c_str(), id)) {
        return std::nullopt;
    }
    return guarded(
        "cannot read object " + objectId + " from the results store in " + m_impl->directory,
        [&]() -> std::optional<core::ResultDetails> {
            sp::ObjectRecord record;
            if (!m_impl->store->objects().get(id, record)) {
                return std::nullopt;
            }
            const sp::VocabularyRegistry& vocabulary = m_impl->store->vocabulary();
            const sp::AnnotationStore& annotations = m_impl->store->annotations();

            core::ResultDetails details;
            details.layer = m_impl->store->layers().name(record.layer());
            for (const sp::LabelId& label : m_impl->effectiveLabels(record, true)) {
                details.classes.emplace_back(vocabulary.name(label));
            }
            details.stage = m_impl->stageName(record.stage());

            const sp::MeasurementSet& own = record.measurements();
            for (int32_t i = 0; i < own.count(); ++i) {
                const sp::Measurement m = own.at(i);
                details.measurements.push_back({m.name(), m.value(), unitText(m.unit()), ""});
            }
            const int32_t asserted = annotations.assertedMeasurementCount(id);
            for (int32_t i = 0; i < asserted; ++i) {
                const sp::Measurement m = annotations.assertedMeasurementAt(id, i);
                details.measurements.push_back({m.name(), m.value(), unitText(m.unit()),
                                                m_impl->stageName(annotations.measurementAssertedBy(id, i))});
            }
            return details;
        });
}

void ProcessorWorkspaceSource::scan(
    int layer, const std::function<void(const core::Rect<double>&, const std::vector<int>&)>& visit,
    const std::function<bool(double)>& progress)
{
    const Impl::Layer& entry = m_impl->layerAt(layer);
    guarded("cannot scan the results store in " + m_impl->directory, [&] {
        sp::ObjectStore& objects = m_impl->store->objects();
        sp::ObjectQuery query;
        query.layer(entry.id);
        const int64_t total = objects.count(query);
        if (progress && !progress(0.0)) {
            return;
        }
        const bool anyAssertions = m_impl->anyLabelAssertions();
        sp::CursorPtr cursor = objects.query(query);
        int64_t visited = 0;
        while (cursor->next()) {
            const sp::ObjectRecord& record = cursor->current();
            visit(toRect(record.boundingBox()), m_impl->classesOf(entry, record, anyAssertions));
            ++visited;
            if (progress && total > 0 && visited % kScanProgressInterval == 0 &&
                !progress(std::min(1.0, static_cast<double>(visited) / static_cast<double>(total)))) {
                cursor->close();
                return;
            }
        }
        cursor->close();
        if (progress) {
            progress(1.0);
        }
    });
}

} // namespace slideio::viewer::infra
