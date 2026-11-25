#include "slideio/viewer/app/ResultsOverlayService.h"

#include "slideio/viewer/core/ResultGeometry.h"
#include "slideio/viewer/core/ResultsPalette.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace slideio::viewer::app
{

namespace
{

bool inside(const core::Rect<double>& inner, const core::Rect<double>& outer)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width
        && inner.y + inner.height <= outer.y + outer.height;
}

/// The view grown by half its width on the left and right and half its height
/// on the top and bottom.
core::Rect<double> grown(const core::Rect<double>& view)
{
    return {view.x - view.width * 0.5, view.y - view.height * 0.5, view.width * 2.0, view.height * 2.0};
}

bool contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

/// Message of the exception in flight; call inside a catch block only.
std::string currentErrorMessage()
{
    try {
        throw;
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown results source error";
    }
}

const ResultsOverlayService::LayerState kNoLayerState{};
const core::ResultsLayerView kNoLayerView{};

/// Scan progress is posted when it has advanced by at least this much.
constexpr double kProgressStep = 0.01;

} // namespace

ResultsOverlayService::ResultsOverlayService(Poster post)
    : m_post(std::move(post))
{
    m_queryThread = std::thread([this] { queryLoop(); });
    m_scanThread = std::thread([this] { scanLoop(); });
}

ResultsOverlayService::~ResultsOverlayService()
{
    detach();
    {
        // Workers copy m_life only under this mutex and only while current(),
        // which is false from here on, so resetting it here races with nothing.
        std::lock_guard<std::mutex> lock(m_postMutex);
        m_stopping = true;
        m_life.reset();   // tasks already queued on the UI thread become no-ops
    }
    // Taking each worker's mutex before notifying means a worker between its
    // predicate check and its wait cannot miss the stop.
    {
        std::lock_guard<std::mutex> lock(m_queryMutex);
    }
    m_queryCv.notify_all();
    {
        std::lock_guard<std::mutex> lock(m_scanMutex);
    }
    m_scanCv.notify_all();
    m_queryThread.join();
    m_scanThread.join();
}

// ---------------------------------------------------------------- attachment

void ResultsOverlayService::attach(SourceFactory factory, const core::Rect<double>& sceneRect)
{
    detach();
    std::shared_ptr<core::ResultsSource> source = factory();
    if (!source)
        throw core::ResultsError("cannot open results");
    auto layers = source->layers();

    m_factory = std::move(factory);
    m_sceneRect = sceneRect;
    m_views.clear();
    startGeneration(std::move(source), std::move(layers));
}

void ResultsOverlayService::detach()
{
    {
        std::lock_guard<std::mutex> lock(m_postMutex);
        ++m_generation;
    }
    {
        std::lock_guard<std::mutex> lock(m_queryMutex);
        m_queryJob.reset();
        m_detailsJob.reset();
    }
    {
        std::lock_guard<std::mutex> lock(m_scanMutex);
        m_scanJob.reset();
    }
    m_attached = false;
    m_factory = nullptr;
    m_source.reset();
    m_layers.clear();
    m_views.clear();
    m_data.clear();
    m_lastView.reset();
    m_lastGrown.reset();
    clearSelection();
}

void ResultsOverlayService::reload()
{
    if (!m_attached)
        return;
    std::shared_ptr<core::ResultsSource> source;
    std::vector<core::ResultLayerInfo> layers;
    try {
        source = m_factory();
        if (!source)
            throw core::ResultsError("cannot open results");
        layers = source->layers();
    } catch (const core::ResultsError&) {
        detach();
        throw;
    }

    // Carry the selection over by layer and class name.
    std::optional<core::ResultObject> selected = m_selectedObject;
    if (selected) {
        const auto oldLayer = static_cast<std::size_t>(selected->layer);
        std::vector<std::string> classNames;
        if (oldLayer < m_layers.size())
            for (const int c : selected->classes)
                if (c >= 0 && static_cast<std::size_t>(c) < m_layers[oldLayer].classes.size())
                    classNames.push_back(m_layers[oldLayer].classes[static_cast<std::size_t>(c)].name);
        selected->layer = -1;
        selected->classes.clear();
        for (std::size_t i = 0; oldLayer < m_layers.size() && i < layers.size(); ++i) {
            if (layers[i].name != m_layers[oldLayer].name)
                continue;
            selected->layer = static_cast<int>(i);
            for (const auto& name : classNames)
                for (std::size_t c = 0; c < layers[i].classes.size(); ++c)
                    if (layers[i].classes[c].name == name)
                        selected->classes.push_back(static_cast<int>(c));
            break;
        }
    }
    const std::optional<std::string> selectedId = m_selectedId;
    const auto lastView = m_lastView;

    startGeneration(std::move(source), std::move(layers));

    if (selected && selected->layer >= 0) {
        m_selectedId = selectedId;
        m_selectedObject = selected;
        clearSelectionIfHidden();
        if (m_selectedId)
            requestDetails(*m_selectedId);   // no details back means the object is gone
    }
    if (lastView)
        issueQuery(*lastView);
}

void ResultsOverlayService::startGeneration(std::shared_ptr<core::ResultsSource> source,
                                            std::vector<core::ResultLayerInfo> layers)
{
    {
        std::lock_guard<std::mutex> lock(m_postMutex);
        ++m_generation;
    }
    {
        std::lock_guard<std::mutex> lock(m_queryMutex);
        m_queryJob.reset();
        m_detailsJob.reset();
    }

    // Views are kept by layer name; a new layer gets the default view.
    std::vector<core::ResultsLayerView> views;
    views.reserve(layers.size());
    for (const auto& layer : layers) {
        const auto it = std::find_if(m_views.begin(), m_views.end(),
                                     [&](const core::ResultsLayerView& v) { return v.layerName == layer.name; });
        core::ResultsLayerView view = it != m_views.end() ? *it : core::ResultsLayerView{};
        view.layerName = layer.name;
        views.push_back(std::move(view));
    }

    m_source = std::move(source);
    m_layers = std::move(layers);
    m_views = std::move(views);
    m_data.assign(m_layers.size(), LayerData{});
    m_lastView.reset();
    m_lastGrown.reset();
    m_settingsChanged = true;
    clearSelection();
    m_attached = true;
    startScans();
}

bool ResultsOverlayService::attached() const
{
    return m_attached;
}

const std::vector<core::ResultLayerInfo>& ResultsOverlayService::layers() const
{
    return m_layers;
}

// ---------------------------------------------------------------- queries

void ResultsOverlayService::requestView(const core::Rect<double>& visibleSlideRect)
{
    if (!m_attached)
        return;
    if (!m_settingsChanged && m_lastGrown && inside(visibleSlideRect, *m_lastGrown)) {
        m_lastView = visibleSlideRect;
        return;
    }
    issueQuery(visibleSlideRect);
}

void ResultsOverlayService::issueQuery(const core::Rect<double>& view)
{
    m_lastView = view;
    QueryJob job;
    for (std::size_t i = 0; i < m_views.size(); ++i)
        if (m_views[i].visible)
            job.layers.push_back(static_cast<int>(i));
    if (job.layers.empty())
        return;
    job.generation = m_generation;
    job.source = m_source;
    job.rect = grown(view);
    m_lastGrown = job.rect;
    m_settingsChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_queryMutex);
        m_queryJob = std::move(job);   // replaces a request not yet started
    }
    m_queryCv.notify_all();
}

void ResultsOverlayService::requestDetails(const std::string& objectId)
{
    DetailsJob job;
    job.generation = m_generation;
    job.source = m_source;
    job.objectId = objectId;
    {
        std::lock_guard<std::mutex> lock(m_queryMutex);
        m_detailsJob = std::move(job);
    }
    m_queryCv.notify_all();
}

void ResultsOverlayService::queryLoop()
{
    for (;;) {
        std::optional<QueryJob> query;
        std::optional<DetailsJob> details;
        {
            std::unique_lock<std::mutex> lock(m_queryMutex);
            m_queryCv.wait(lock, [this] { return m_stopping || m_queryJob || m_detailsJob; });
            if (m_stopping)
                return;
            query.swap(m_queryJob);
            details.swap(m_detailsJob);
        }

        if (query && current(query->generation)) {
            const uint64_t generation = query->generation;
            try {
                auto answers = std::make_shared<std::vector<std::pair<int, core::ResultQueryAnswer>>>();
                bool complete = true;
                for (const int layer : query->layers) {
                    if (!current(generation)) {
                        complete = false;
                        break;
                    }
                    answers->emplace_back(layer, query->source->query(layer, query->rect, core::kResultsDrawBudget));
                }
                if (complete) {
                    post(generation, [this, answers] {
                        for (auto& [layer, answer] : *answers) {
                            const auto l = static_cast<std::size_t>(layer);
                            if (l >= m_data.size() || !m_views[l].visible)
                                continue;   // hidden since the request
                            auto& data = m_data[l];
                            data.hasAnswer = true;
                            data.overBudget = answer.overBudget;
                            if (answer.overBudget) {
                                data.state.objects.clear();
                                data.state.inView = answer.inView;
                            } else {
                                data.state.objects = std::move(answer.objects);
                                data.state.inView = static_cast<int64_t>(data.state.objects.size());
                            }
                            refreshMode(layer);
                        }
                        notifyChanged();
                    });
                }
            } catch (...) {
                postError(generation, currentErrorMessage());
            }
        }

        if (details && current(details->generation)) {
            const uint64_t generation = details->generation;
            try {
                auto result = details->source->details(details->objectId);
                post(generation, [this, id = details->objectId, result = std::move(result)] {
                    if (m_selectedId != id)
                        return;   // selection moved on
                    if (result)
                        m_selectedDetails = result;
                    else
                        clearSelection();   // the object is gone
                    notifyChanged();
                });
            } catch (...) {
                postError(generation, currentErrorMessage());
            }
        }
    }
}

// ---------------------------------------------------------------- scans

void ResultsOverlayService::startScans()
{
    ScanJob job;
    job.generation = m_generation;
    job.factory = m_factory;
    job.sceneRect = m_sceneRect;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < m_layers.size(); ++i)
        if (m_layers[i].objectCount > core::kResultsDrawBudget)
            order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        return m_layers[a].objectCount > m_layers[b].objectCount;
    });
    for (const auto i : order) {
        ScanLayer layer;
        layer.layer = static_cast<int>(i);
        for (const auto& c : m_layers[i].classes)
            layer.classCounts.push_back(c.objectCount);
        job.layers.push_back(std::move(layer));
    }
    {
        std::lock_guard<std::mutex> lock(m_scanMutex);
        if (job.layers.empty())
            m_scanJob.reset();
        else
            m_scanJob = std::move(job);
    }
    m_scanCv.notify_all();
}

void ResultsOverlayService::scanLoop()
{
    for (;;) {
        std::optional<ScanJob> job;
        {
            std::unique_lock<std::mutex> lock(m_scanMutex);
            m_scanCv.wait(lock, [this] { return m_stopping || m_scanJob; });
            if (m_stopping)
                return;
            job.swap(m_scanJob);
        }
        if (!current(job->generation))
            continue;
        try {
            runScan(*job);
        } catch (...) {
            postError(job->generation, currentErrorMessage());
        }
    }
}

void ResultsOverlayService::runScan(const ScanJob& job)
{
    const uint64_t generation = job.generation;
    const std::unique_ptr<core::ResultsSource> source = job.factory();   // the scan thread's own source
    if (!source)
        throw core::ResultsError("cannot open results");

    for (const auto& scanLayer : job.layers) {
        if (!current(generation))
            return;
        const int layer = scanLayer.layer;
        auto grid = std::make_shared<core::OverviewGrid>(job.sceneRect, scanLayer.classCounts);
        double lastPosted = -1.0;
        bool cancelled = false;
        bool addFailed = false;
        // Neither callback may throw: the source would report it as its own
        // failure. Cancelling is progress returning false.
        source->scan(
            layer,
            [&](const core::Rect<double>& bounds, const std::vector<int>& classes) noexcept {
                if (addFailed)
                    return;
                try {
                    grid->add(bounds, classes);
                } catch (...) {
                    addFailed = true;
                }
            },
            [&](double fraction) noexcept {
                if (addFailed)
                    return false;
                if (!current(generation)) {
                    cancelled = true;
                    return false;
                }
                if (fraction - lastPosted >= kProgressStep && fraction < 1.0) {
                    lastPosted = fraction;
                    try {
                        post(generation, [this, layer, fraction] {
                            const auto l = static_cast<std::size_t>(layer);
                            if (l >= m_data.size() || m_data[l].state.grid)
                                return;
                            m_data[l].state.scanFraction = fraction;
                            notifyChanged();
                        });
                    } catch (...) {
                        // A lost progress update is harmless.
                    }
                }
                return true;
            });
        if (addFailed)
            throw core::ResultsError("cannot build the overview of layer " + std::to_string(layer));
        if (cancelled || !current(generation))
            return;
        grid->finish();
        std::shared_ptr<const core::OverviewGrid> finished = std::move(grid);
        post(generation, [this, layer, finished] {
            const auto l = static_cast<std::size_t>(layer);
            if (l >= m_data.size())
                return;
            m_data[l].state.grid = finished;
            m_data[l].state.scanFraction = 1.0;
            refreshMode(layer);
            notifyChanged();
        });
    }
}

// ---------------------------------------------------------------- posting

bool ResultsOverlayService::current(uint64_t generation) const
{
    return !m_stopping && m_generation == generation;
}

void ResultsOverlayService::post(uint64_t generation, std::function<void()> apply)
{
    // Under the mutex that generation bumps and the destructor's m_life reset
    // take: once detach() returns, no task of an older generation is posted,
    // and m_life is never copied while it is being reset.
    std::lock_guard<std::mutex> lock(m_postMutex);
    if (!current(generation))
        return;
    std::weak_ptr<int> life = m_life;
    m_post([this, life, generation, apply = std::move(apply)] {
        if (life.expired() || m_generation != generation)
            return;
        apply();
    });
}

void ResultsOverlayService::postError(uint64_t generation, const std::string& message)
{
    post(generation, [this, message] {
        detach();
        if (m_onError)
            m_onError(message);
        notifyChanged();
    });
}

void ResultsOverlayService::notifyChanged()
{
    if (m_onChanged)
        m_onChanged();
}

// ---------------------------------------------------------------- layers

const ResultsOverlayService::LayerState& ResultsOverlayService::layerState(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= m_data.size())
        return kNoLayerState;
    return m_data[static_cast<std::size_t>(layer)].state;
}

void ResultsOverlayService::refreshMode(int layer)
{
    const auto l = static_cast<std::size_t>(layer);
    auto& data = m_data[l];
    if (!m_views[l].visible || !data.hasAnswer)
        data.state.mode = LayerMode::Hidden;
    else if (!data.overBudget)
        data.state.mode = LayerMode::Outlines;
    else if (data.state.grid)
        data.state.mode = LayerMode::Overview;
    else
        data.state.mode = LayerMode::BuildingOverview;   // also a layer too small to be scanned
}

void ResultsOverlayService::setLayerView(int layer, const core::ResultsLayerView& view)
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= m_views.size())
        return;
    const auto l = static_cast<std::size_t>(layer);
    const bool wasVisible = m_views[l].visible;
    m_views[l] = view;
    m_views[l].layerName = m_layers[l].name;

    if (wasVisible && !view.visible) {
        auto& data = m_data[l];
        data.hasAnswer = false;
        data.overBudget = false;
        data.state.objects.clear();
        data.state.inView = 0;
    }
    refreshMode(layer);
    clearSelectionIfHidden();

    if (!wasVisible && view.visible) {
        m_settingsChanged = true;
        if (m_lastView)
            issueQuery(*m_lastView);
    }
}

const core::ResultsLayerView& ResultsOverlayService::layerView(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= m_views.size())
        return kNoLayerView;
    return m_views[static_cast<std::size_t>(layer)];
}

bool ResultsOverlayService::objectVisible(const core::ResultObject& object) const
{
    if (object.layer < 0 || static_cast<std::size_t>(object.layer) >= m_views.size())
        return false;
    const auto l = static_cast<std::size_t>(object.layer);
    const auto& view = m_views[l];
    if (!view.visible)
        return false;
    if (object.classes.empty())
        return !contains(view.hiddenClasses, kUnclassifiedName);
    const auto& classes = m_layers[l].classes;
    for (const int c : object.classes) {
        if (c < 0 || static_cast<std::size_t>(c) >= classes.size())
            return true;   // a class the legend does not list cannot be hidden
        if (!contains(view.hiddenClasses, classes[static_cast<std::size_t>(c)].name))
            return true;
    }
    return false;
}

void ResultsOverlayService::setClassColourOverrides(std::map<std::string, core::Color> byFoldedName)
{
    m_classOverrides = std::move(byFoldedName);
}

std::vector<core::Color> ResultsOverlayService::classColours(int layer) const
{
    std::vector<core::Color> colours;
    if (layer < 0 || static_cast<std::size_t>(layer) >= m_layers.size())
        return colours;
    const auto& classes = m_layers[static_cast<std::size_t>(layer)].classes;
    colours.reserve(classes.size());
    for (std::size_t i = 0; i < classes.size(); ++i)
        colours.push_back(core::resultClassColour(classes[i].name, static_cast<int>(i), m_classOverrides));
    return colours;
}

core::Color ResultsOverlayService::layerColour(int layer) const
{
    if (layer >= 0 && static_cast<std::size_t>(layer) < m_views.size()) {
        const auto& colour = m_views[static_cast<std::size_t>(layer)].colour;
        if (colour)
            return *colour;
    }
    return core::resultLayerColour(layer);
}

// ---------------------------------------------------------------- selection

std::optional<std::string> ResultsOverlayService::selectAt(core::PointF slidePoint)
{
    for (int layer = static_cast<int>(m_data.size()) - 1; layer >= 0; --layer) {
        const auto& state = m_data[static_cast<std::size_t>(layer)].state;
        if (state.mode != LayerMode::Outlines)
            continue;
        for (auto it = state.objects.rbegin(); it != state.objects.rend(); ++it) {
            if (!objectVisible(*it) || !core::resultContains(*it, slidePoint))
                continue;
            m_selectedId = it->id;
            m_selectedObject = *it;
            m_selectedDetails.reset();
            requestDetails(it->id);
            return m_selectedId;
        }
    }
    clearSelection();
    return std::nullopt;
}

void ResultsOverlayService::clearSelection()
{
    m_selectedId.reset();
    m_selectedObject.reset();
    m_selectedDetails.reset();
}

void ResultsOverlayService::clearSelectionIfHidden()
{
    if (m_selectedObject && !objectVisible(*m_selectedObject))
        clearSelection();
}

const std::optional<std::string>& ResultsOverlayService::selectedId() const
{
    return m_selectedId;
}

const std::optional<core::ResultDetails>& ResultsOverlayService::selectedDetails() const
{
    return m_selectedDetails;
}

void ResultsOverlayService::setOnChanged(std::function<void()> callback)
{
    m_onChanged = std::move(callback);
}

void ResultsOverlayService::setOnError(std::function<void(const std::string&)> callback)
{
    m_onError = std::move(callback);
}

} // namespace slideio::viewer::app
