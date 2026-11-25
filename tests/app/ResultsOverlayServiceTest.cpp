#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/ResultsOverlayService.h"
#include "slideio/viewer/core/OverviewGrid.h"
#include "slideio/viewer/core/ResultsPalette.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace slideio::viewer;
using Service = app::ResultsOverlayService;
using Mode = Service::LayerMode;

namespace
{

constexpr auto kTimeout = std::chrono::seconds(20);

/// A gate a worker passes through; closed, it holds the worker until opened.
class Gate
{
public:
    void close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    void open()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = false;
        }
        m_cv.notify_all();
    }
    void pass()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        ++m_entered;
        m_cv.notify_all();
        m_cv.wait(lock, [this] { return !m_closed; });
    }
    bool waitEntered(int count)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kTimeout, [&] { return m_entered >= count; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_closed = false;
    int m_entered = 0;
};

/// State shared by every FakeResultsSource one factory opens, so a test sees
/// the calls the query source and the scan source both made.
struct FakeStore
{
    std::vector<core::ResultLayerInfo> layers;
    std::vector<std::vector<core::ResultObject>> objects;   // per layer
    std::vector<bool> overBudget;                           // per layer: query answers overBudget
    std::map<std::string, core::ResultDetails> details;
    std::string queryError;                                 // non-empty: query throws it

    Gate queryGate;
    Gate scanGate;

    std::mutex mutex;
    std::condition_variable cv;
    int factoryCalls = 0;
    std::vector<std::pair<int, core::Rect<double>>> queries;
    std::vector<int64_t> budgets;
    int queriesReturned = 0;
    std::vector<int> scans;              // layer of each scan started
    std::vector<bool> scanCancelled;     // per scan: progress returned false
    std::vector<std::string> detailsCalls;

    template<typename Pred>
    bool waitFor(Pred pred)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, kTimeout, [&] { return pred(); });
    }
};

bool intersects(const core::Rect<double>& a, const core::Rect<double>& b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

class FakeResultsSource : public core::ResultsSource
{
public:
    explicit FakeResultsSource(std::shared_ptr<FakeStore> store) : m_store(std::move(store)) {}

    core::ResultsProvenance provenance() const override { return {}; }
    std::vector<core::ResultLayerInfo> layers() const override
    {
        std::lock_guard<std::mutex> lock(m_store->mutex);
        return m_store->layers;
    }

    core::ResultQueryAnswer query(int layer, const core::Rect<double>& rect, int64_t budget) override
    {
        {
            std::lock_guard<std::mutex> lock(m_store->mutex);
            m_store->queries.emplace_back(layer, rect);
            m_store->budgets.push_back(budget);
        }
        m_store->cv.notify_all();
        m_store->queryGate.pass();
        core::ResultQueryAnswer answer;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(m_store->mutex);
            error = m_store->queryError;
            const auto l = static_cast<std::size_t>(layer);
            if (error.empty()) {
                if (l < m_store->overBudget.size() && m_store->overBudget[l]) {
                    answer.overBudget = true;
                    answer.inView = m_store->layers[l].objectCount;
                } else if (l < m_store->objects.size()) {
                    for (const auto& object : m_store->objects[l])
                        if (intersects(object.bounds, rect))
                            answer.objects.push_back(object);
                    answer.inView = static_cast<int64_t>(answer.objects.size());
                }
            }
            ++m_store->queriesReturned;
        }
        m_store->cv.notify_all();
        if (!error.empty())
            throw core::ResultsError(error);
        return answer;
    }

    std::optional<core::ResultDetails> details(const std::string& objectId) override
    {
        std::lock_guard<std::mutex> lock(m_store->mutex);
        m_store->detailsCalls.push_back(objectId);
        m_store->cv.notify_all();
        const auto it = m_store->details.find(objectId);
        if (it == m_store->details.end())
            return std::nullopt;
        return it->second;
    }

    void scan(int layer, const std::function<void(const core::Rect<double>&, const std::vector<int>&)>& visit,
              const std::function<bool(double)>& progress) override
    {
        std::size_t index = 0;
        std::vector<core::ResultObject> objects;
        {
            std::lock_guard<std::mutex> lock(m_store->mutex);
            index = m_store->scans.size();
            m_store->scans.push_back(layer);
            m_store->scanCancelled.push_back(false);
            if (static_cast<std::size_t>(layer) < m_store->objects.size())
                objects = m_store->objects[static_cast<std::size_t>(layer)];
        }
        m_store->cv.notify_all();
        bool keepGoing = progress(0.0);
        m_store->scanGate.pass();
        if (keepGoing) {
            for (const auto& object : objects)
                visit(object.bounds, object.classes);
            keepGoing = progress(1.0);
        }
        {
            std::lock_guard<std::mutex> lock(m_store->mutex);
            m_store->scanCancelled[index] = !keepGoing;
        }
        m_store->cv.notify_all();
    }

private:
    std::shared_ptr<FakeStore> m_store;
};

Service::SourceFactory factoryFor(const std::shared_ptr<FakeStore>& store)
{
    return [store]() -> std::unique_ptr<core::ResultsSource> {
        {
            std::lock_guard<std::mutex> lock(store->mutex);
            ++store->factoryCalls;
        }
        store->cv.notify_all();
        return std::make_unique<FakeResultsSource>(store);
    };
}

/// The "UI thread": the poster appends; the test drains on its own thread.
class UiQueue
{
public:
    Service::Poster poster()
    {
        return [this](std::function<void()> task) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_tasks.push_back(std::move(task));
                ++m_posted;
            }
            m_cv.notify_all();
        };
    }

    int posted()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_posted;
    }

    bool waitPosted(int count)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, kTimeout, [&] { return m_posted >= count; });
    }

    /// Runs every queued task; returns how many ran.
    int drain()
    {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            tasks.swap(m_tasks);
        }
        for (auto& task : tasks)
            task();
        return static_cast<int>(tasks.size());
    }

    /// Runs tasks as they arrive until `pred` holds; false on timeout.
    template<typename Pred>
    bool drainUntil(Pred pred)
    {
        const auto deadline = std::chrono::steady_clock::now() + kTimeout;
        for (;;) {
            drain();
            if (pred())
                return true;
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_cv.wait_until(lock, deadline, [&] { return !m_tasks.empty(); }))
                return false;
        }
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::function<void()>> m_tasks;
    int m_posted = 0;
};

core::ResultObject square(const std::string& id, int layer, double x, double y, double size,
                          std::vector<int> classes = {})
{
    core::ResultObject object;
    object.id = id;
    object.layer = layer;
    object.classes = std::move(classes);
    core::ResultRing ring;
    ring.points = {{x, y}, {x + size, y}, {x + size, y + size}, {x, y + size}, {x, y}};
    object.rings.push_back(ring);
    object.bounds = {x, y, size, size};
    return object;
}

core::ResultLayerInfo layerInfo(const std::string& name, int64_t count, std::vector<std::string> classes = {})
{
    core::ResultLayerInfo info;
    info.name = name;
    info.objectCount = count;
    for (const auto& c : classes)
        info.classes.push_back({c, 1});
    return info;
}

const core::Rect<double> kScene{0.0, 0.0, 100000.0, 50000.0};

bool sameRect(const core::Rect<double>& a, const core::Rect<double>& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

TEST_CASE("OutlinesUnderBudget", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 2)};
    store->objects = {{square("a", 0, 10, 10, 20), square("b", 0, 50, 50, 20)}};
    UiQueue ui;
    Service service(ui.poster());
    int changed = 0;
    service.setOnChanged([&] { ++changed; });

    service.attach(factoryFor(store), kScene);
    REQUIRE(service.attached());
    REQUIRE(service.layers().size() == 1);
    CHECK(service.layerView(0).layerName == "cells");
    CHECK(service.layerView(0).visible);

    service.requestView({0, 0, 100, 100});
    REQUIRE(ui.drainUntil([&] { return service.layerState(0).mode == Mode::Outlines; }));
    CHECK(service.layerState(0).objects.size() == 2);
    CHECK(service.layerState(0).inView == 2);
    CHECK(changed >= 1);
    std::lock_guard<std::mutex> lock(store->mutex);
    REQUIRE(store->queries.size() == 1);
    CHECK(store->queries[0].first == 0);
    CHECK(sameRect(store->queries[0].second, {-50, -50, 200, 200}));
    CHECK(store->budgets[0] == core::kResultsDrawBudget);
    CHECK(store->factoryCalls == 1);   // no layer over budget: no scan source
    CHECK(store->scans.empty());
}

TEST_CASE("OverviewWhenOverBudgetAfterScan", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("small", 30000, {"x"}), layerInfo("large", 50000, {"y"})};
    store->objects = {{square("s", 0, 10, 10, 5, {0})}, {square("l", 1, 20, 20, 5, {0})}};
    store->overBudget = {true, true};
    store->scanGate.close();
    UiQueue ui;
    Service service(ui.poster());

    service.attach(factoryFor(store), kScene);
    service.requestView({0, 0, 100, 100});
    REQUIRE(ui.drainUntil([&] {
        return service.layerState(0).mode != Mode::Hidden && service.layerState(1).mode != Mode::Hidden;
    }));
    CHECK(service.layerState(0).mode == Mode::BuildingOverview);
    CHECK(service.layerState(1).mode == Mode::BuildingOverview);
    CHECK(service.layerState(1).inView == 50000);
    CHECK(service.layerState(0).objects.empty());
    CHECK(service.layerState(0).grid == nullptr);

    store->scanGate.open();
    REQUIRE(ui.drainUntil([&] {
        return service.layerState(0).mode == Mode::Overview && service.layerState(1).mode == Mode::Overview;
    }));
    REQUIRE(service.layerState(0).grid != nullptr);
    REQUIRE(service.layerState(1).grid != nullptr);
    CHECK(service.layerState(0).scanFraction == 1.0);
    std::lock_guard<std::mutex> lock(store->mutex);
    CHECK(store->factoryCalls == 2);              // the scan thread opened its own source
    REQUIRE(store->scans == std::vector<int>{1, 0});  // largest first
}

TEST_CASE("LatestViewWins", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 1)};
    store->objects = {{square("a", 0, 10, 10, 20)}};
    store->queryGate.close();
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);

    service.requestView({0, 0, 100, 100});
    REQUIRE(store->queryGate.waitEntered(1));
    service.requestView({1000, 1000, 100, 100});
    service.requestView({5000, 5000, 100, 100});
    store->queryGate.open();

    REQUIRE(ui.waitPosted(2));
    ui.drain();
    std::lock_guard<std::mutex> lock(store->mutex);
    REQUIRE(store->queries.size() == 2);
    CHECK(sameRect(store->queries[0].second, {-50, -50, 200, 200}));
    CHECK(sameRect(store->queries[1].second, {4950, 4950, 200, 200}));
}

TEST_CASE("SmallPanReusesGrownRect", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 1)};
    store->objects = {{square("a", 0, 10, 10, 20)}};
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);

    service.requestView({0, 0, 100, 100});
    REQUIRE(ui.drainUntil([&] { return service.layerState(0).mode == Mode::Outlines; }));
    service.requestView({40, -40, 100, 100});     // inside (-50,-50)-(150,150)
    service.requestView({300, 300, 100, 100});    // outside
    REQUIRE(store->waitFor([&] { return store->queries.size() >= 2; }));
    REQUIRE(ui.waitPosted(2));
    ui.drain();
    std::lock_guard<std::mutex> lock(store->mutex);
    REQUIRE(store->queries.size() == 2);
    CHECK(sameRect(store->queries[1].second, {250, 250, 200, 200}));
}

TEST_CASE("HiddenClassHidesObjectOnlyWhenAllItsClassesHidden", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 3, {"A", "B"})};
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);

    const auto onlyA = square("1", 0, 0, 0, 1, {0});
    const auto both = square("2", 0, 0, 0, 1, {0, 1});
    const auto none = square("3", 0, 0, 0, 1);

    CHECK(service.objectVisible(onlyA));
    CHECK(service.objectVisible(both));
    CHECK(service.objectVisible(none));

    auto view = service.layerView(0);
    view.hiddenClasses = {"A"};
    service.setLayerView(0, view);
    CHECK_FALSE(service.objectVisible(onlyA));
    CHECK(service.objectVisible(both));
    CHECK(service.objectVisible(none));

    view.hiddenClasses = {"A", "B"};
    service.setLayerView(0, view);
    CHECK_FALSE(service.objectVisible(both));
    CHECK(service.objectVisible(none));

    view.hiddenClasses = {Service::kUnclassifiedName};
    service.setLayerView(0, view);
    CHECK(service.objectVisible(onlyA));
    CHECK_FALSE(service.objectVisible(none));
}

TEST_CASE("SelectTopLayerFirst", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("tissue", 1), layerInfo("cells", 2)};
    store->objects = {{square("tissue-1", 0, 0, 0, 100)},
                      {square("cell-1", 1, 40, 40, 20), square("cell-2", 1, 45, 45, 10)}};
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);
    service.requestView({0, 0, 100, 100});
    REQUIRE(ui.drainUntil([&] {
        return service.layerState(0).mode == Mode::Outlines && service.layerState(1).mode == Mode::Outlines;
    }));

    CHECK(service.selectAt({50, 50}) == std::optional<std::string>("cell-2"));   // top layer, last object
    CHECK(service.selectedId() == std::optional<std::string>("cell-2"));
    CHECK(service.selectAt({42, 42}) == std::optional<std::string>("cell-1"));
    CHECK(service.selectAt({10, 10}) == std::optional<std::string>("tissue-1"));

    auto view = service.layerView(1);
    view.visible = false;
    service.setLayerView(1, view);
    CHECK(service.layerState(1).mode == Mode::Hidden);
    CHECK(service.selectAt({50, 50}) == std::optional<std::string>("tissue-1"));

    CHECK_FALSE(service.selectAt({500, 500}).has_value());
    CHECK_FALSE(service.selectedId().has_value());
}

TEST_CASE("DetailsArrivePosted", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 1)};
    store->objects = {{square("a", 0, 10, 10, 20)}};
    core::ResultDetails details;
    details.layer = "cells";
    details.stage = "segment";
    details.measurements = {{"area", 400.0, "px2", ""}};
    store->details["a"] = details;
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);
    service.requestView({0, 0, 100, 100});
    REQUIRE(ui.drainUntil([&] { return service.layerState(0).mode == Mode::Outlines; }));

    REQUIRE(service.selectAt({15, 15}) == std::optional<std::string>("a"));
    CHECK_FALSE(service.selectedDetails().has_value());   // nothing drained yet
    REQUIRE(ui.drainUntil([&] { return service.selectedDetails().has_value(); }));
    CHECK(service.selectedDetails()->stage == "segment");
    REQUIRE(service.selectedDetails()->measurements.size() == 1);
    CHECK(service.selectedDetails()->measurements[0].value == 400.0);
    std::lock_guard<std::mutex> lock(store->mutex);
    CHECK(store->detailsCalls == std::vector<std::string>{"a"});
}

TEST_CASE("DetachDropsInFlightResults", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 1)};
    store->objects = {{square("old", 0, 10, 10, 20)}};
    store->queryGate.close();
    UiQueue ui;
    Service service(ui.poster());
    int changed = 0;
    service.setOnChanged([&] { ++changed; });
    service.attach(factoryFor(store), kScene);
    service.requestView({0, 0, 100, 100});
    REQUIRE(store->queryGate.waitEntered(1));

    service.detach();
    CHECK_FALSE(service.attached());
    CHECK(service.layers().empty());
    {
        std::lock_guard<std::mutex> lock(store->mutex);
        store->objects = {{square("new", 0, 10, 10, 20)}};
    }
    store->queryGate.open();

    // Queries run one at a time: once the new attachment's answer is posted,
    // the detached query has finished and had its chance to post.
    service.attach(factoryFor(store), kScene);
    service.requestView({0, 0, 100, 100});
    REQUIRE(ui.waitPosted(1));
    REQUIRE(store->waitFor([&] { return store->queriesReturned == 2; }));
    REQUIRE(ui.drainUntil([&] { return service.layerState(0).mode == Mode::Outlines; }));
    CHECK(ui.posted() == 1);
    CHECK(changed == 1);
    REQUIRE(service.layerState(0).objects.size() == 1);
    CHECK(service.layerState(0).objects[0].id == "new");
}

TEST_CASE("ReloadRestartsScan", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 30000)};
    store->objects = {{square("a", 0, 10, 10, 5)}};
    store->overBudget = {true};
    store->scanGate.close();
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);
    service.requestView({0, 0, 100, 100});
    REQUIRE(store->scanGate.waitEntered(1));

    service.reload();
    CHECK(service.attached());
    store->scanGate.open();
    REQUIRE(ui.drainUntil([&] { return service.layerState(0).mode == Mode::Overview; }));
    CHECK(service.layerState(0).grid != nullptr);
    std::lock_guard<std::mutex> lock(store->mutex);
    REQUIRE(store->scans.size() == 2);
    CHECK(store->scanCancelled[0]);
    CHECK_FALSE(store->scanCancelled[1]);
    CHECK(store->factoryCalls == 4);   // attach, scan, reload, rescan
    CHECK(store->queries.size() == 2); // reload re-queried the last view
}

TEST_CASE("SourceErrorDetachesAndReports", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 1)};
    store->queryError = "gone";
    store->queryGate.close();
    UiQueue ui;
    Service service(ui.poster());
    std::vector<std::string> errors;
    service.setOnError([&](const std::string& message) { errors.push_back(message); });
    service.attach(factoryFor(store), kScene);

    service.requestView({0, 0, 100, 100});
    REQUIRE(store->queryGate.waitEntered(1));
    service.requestView({1000, 1000, 100, 100});
    store->queryGate.open();
    REQUIRE(ui.waitPosted(2));   // both queries failed and posted
    ui.drain();

    CHECK(errors == std::vector<std::string>{"gone"});
    CHECK_FALSE(service.attached());
    CHECK(service.layers().empty());
}

TEST_CASE("HidingClassClearsSelection", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 2, {"A", "B"})};
    store->objects = {{square("ab", 0, 10, 10, 20, {0, 1}), square("plain", 0, 60, 60, 20)}};
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);
    service.requestView({0, 0, 100, 100});
    REQUIRE(ui.drainUntil([&] { return service.layerState(0).mode == Mode::Outlines; }));

    REQUIRE(service.selectAt({15, 15}) == std::optional<std::string>("ab"));
    auto view = service.layerView(0);
    view.hiddenClasses = {"A"};
    service.setLayerView(0, view);
    CHECK(service.selectedId() == std::optional<std::string>("ab"));
    view.hiddenClasses = {"A", "B"};
    service.setLayerView(0, view);
    CHECK_FALSE(service.selectedId().has_value());
    CHECK_FALSE(service.selectAt({15, 15}).has_value());   // hidden objects are not hit

    REQUIRE(service.selectAt({65, 65}) == std::optional<std::string>("plain"));
    view.hiddenClasses = {};
    view.visible = false;
    service.setLayerView(0, view);
    CHECK_FALSE(service.selectedId().has_value());

    view.visible = true;
    service.setLayerView(0, view);
    REQUIRE(ui.drainUntil([&] { return service.layerState(0).mode == Mode::Outlines; }));
    REQUIRE(service.selectAt({65, 65}) == std::optional<std::string>("plain"));
    service.detach();
    CHECK_FALSE(service.selectedId().has_value());
}

TEST_CASE("DestructorJoinsWorkers", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 30000), layerInfo("tissue", 1)};
    store->objects = {{square("a", 0, 10, 10, 5)}, {square("t", 1, 0, 0, 50)}};
    store->overBudget = {true, false};
    store->queryGate.close();
    store->scanGate.close();
    UiQueue ui;
    int changed = 0;
    auto service = std::make_unique<Service>(ui.poster());
    service->setOnChanged([&] { ++changed; });
    service->attach(factoryFor(store), kScene);
    service->requestView({0, 0, 100, 100});
    REQUIRE(store->queryGate.waitEntered(1));
    REQUIRE(store->scanGate.waitEntered(1));

    std::thread releaser([&] {
        store->queryGate.open();
        store->scanGate.open();
    });
    service.reset();   // joins both workers, whatever the releaser's timing
    releaser.join();

    ui.drain();
    CHECK(changed == 0);
    std::lock_guard<std::mutex> lock(store->mutex);
    CHECK(store->queriesReturned >= 1);
}

TEST_CASE("ClassAndLayerColours", "[ResultsOverlayService]")
{
    auto store = std::make_shared<FakeStore>();
    store->layers = {layerInfo("cells", 2, {"Tumour", "Stroma"}), layerInfo("tissue", 1)};
    UiQueue ui;
    Service service(ui.poster());
    service.attach(factoryFor(store), kScene);

    const core::Color red{255, 0, 0, 255};
    service.setClassColourOverrides({{"tumour", red}});
    const auto colours = service.classColours(0);
    REQUIRE(colours.size() == 2);
    const auto expected0 = core::resultClassColour("Tumour", 0, {{"tumour", red}});
    const auto expected1 = core::resultClassColour("Stroma", 1, {{"tumour", red}});
    CHECK((colours[0].r == expected0.r && colours[0].g == expected0.g && colours[0].b == expected0.b));
    CHECK((colours[1].r == expected1.r && colours[1].g == expected1.g && colours[1].b == expected1.b));

    const auto layer1 = core::resultLayerColour(1);
    CHECK((service.layerColour(1).r == layer1.r && service.layerColour(1).b == layer1.b));
    auto view = service.layerView(1);
    view.colour = core::Color{1, 2, 3, 255};
    service.setLayerView(1, view);
    CHECK((service.layerColour(1).r == 1 && service.layerColour(1).g == 2 && service.layerColour(1).b == 3));
}
