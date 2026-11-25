#pragma once

#include "slideio/viewer/core/OverviewGrid.h"
#include "slideio/viewer/core/ResultsAttachment.h"
#include "slideio/viewer/core/ResultsSource.h"
#include "slideio/viewer/core/Types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace slideio::viewer::app
{

/// Owns an attached processing-results source and what the viewport draws
/// from it. Qt-free: two worker threads (queries + details, overview scans)
/// hand their results to the UI thread through the Poster.
///
/// Every public member is called on the UI thread, and every posted task runs
/// there. The only store reads on the UI thread are the layer metadata reads in
/// attach() and reload(); queries and details run on the query thread through
/// the source attach() opened, scans on the scan thread through a second
/// source: the first source's sibling() when it offers one, else the factory.
/// Each posted task carries the attach generation it was made for and does nothing once detach(), reload(), attach() or the
/// destructor has run since. The Poster must queue the task, never run it
/// synchronously, and must not call back into the service.
class ResultsOverlayService
{
public:
    using Poster = std::function<void(std::function<void()>)>;
    using SourceFactory = std::function<std::unique_ptr<core::ResultsSource>()>;

    /// The name hiddenClasses uses for a layer's "Unclassified" legend row.
    static constexpr const char* kUnclassifiedName = "";

    /// A factory whose first call returns `opened` -- a source the caller has
    /// already opened, to read its provenance -- and whose later calls call
    /// `open`. So an attach opens the store once. Safe to call on any thread.
    static SourceFactory reuseFirst(std::unique_ptr<core::ResultsSource> opened, SourceFactory open);

    explicit ResultsOverlayService(Poster post);
    ~ResultsOverlayService();

    ResultsOverlayService(const ResultsOverlayService&) = delete;
    ResultsOverlayService& operator=(const ResultsOverlayService&) = delete;

    /// Opens a source, reads its layers on the calling thread and starts the
    /// overview scans. Detaches first when attached. Throws core::ResultsError
    /// (left detached) when the source cannot be opened or read.
    void attach(SourceFactory factory, const core::Rect<double>& sceneRect);
    void detach();
    /// Re-reads layers through a new source, keeps layer views by name,
    /// restarts scans and re-queries the last view. On a ResultsError it
    /// detaches and rethrows.
    void reload();
    bool attached() const;
    const std::vector<core::ResultLayerInfo>& layers() const;

    /// Latest request wins. Each visible layer is queried over the view grown
    /// by half its size on every side; a layer over budget there is queried
    /// again over the view itself, and that answer decides its mode and count.
    /// Does nothing when detached or no layer is visible. Reuses the last
    /// answers -- recounting inView over the new view -- when no setting
    /// changed and either the view is the last one queried, or it lies inside
    /// every rectangle the visible layers' objects cover, no visible layer was
    /// over budget, and its size is within kZoomRequeryFactor of the last
    /// queried view's.
    void requestView(const core::Rect<double>& visibleSlideRect);

    /// A view whose width or height differs from the last queried view's by
    /// more than this factor is a zoom, and is queried again.
    static constexpr double kZoomRequeryFactor = 1.25;

    enum class LayerMode { Hidden, Outlines, Overview, BuildingOverview };
    struct LayerState
    {
        LayerMode mode = LayerMode::Hidden;
        int64_t inView = 0;   ///< objects intersecting the visible rectangle
        double scanFraction = 0.0;
        std::vector<core::ResultObject> objects;
        std::shared_ptr<const core::OverviewGrid> grid;
        /// An overview scan of this layer was queued at attach or reload (its
        /// object count was over budget). False: BuildingOverview never ends
        /// until a Reload.
        bool scanQueued = false;
    };
    const LayerState& layerState(int layer) const;

    void setLayerView(int layer, const core::ResultsLayerView& view);
    const core::ResultsLayerView& layerView(int layer) const;
    /// The object's layer is visible and any of its classes is visible, or it
    /// has none and Unclassified is visible.
    bool objectVisible(const core::ResultObject& object) const;

    /// Overrides keyed by core::caseFoldKey of the class name.
    void setClassColourOverrides(std::map<std::string, core::Color> byFoldedName);
    /// The colour of each class of `layer`, in legend order.
    std::vector<core::Color> classColours(int layer) const;
    /// The layer view's colour when set, else core::resultLayerColour(layer).
    core::Color layerColour(int layer) const;

    /// Hit-tests the visible Outlines layers, highest index first; within a
    /// layer the last visible object containing the point wins. Requests its
    /// details on the query thread. A miss clears the selection.
    std::optional<std::string> selectAt(core::PointF slidePoint);
    void clearSelection();
    const std::optional<std::string>& selectedId() const;
    const std::optional<core::ResultDetails>& selectedDetails() const;

    /// Called on the UI thread after each applied posted update.
    void setOnChanged(std::function<void()> callback);
    /// A source error on a worker: posted; the service detaches, then calls it.
    void setOnError(std::function<void(const std::string&)> callback);

private:
    struct QueryJob
    {
        uint64_t generation = 0;
        std::shared_ptr<core::ResultsSource> source;
        core::Rect<double> rect;   ///< the view grown
        core::Rect<double> view;   ///< the visible rectangle
        std::vector<int> layers;
    };
    struct DetailsJob
    {
        uint64_t generation = 0;
        std::shared_ptr<core::ResultsSource> source;
        std::string objectId;
    };
    struct ScanLayer
    {
        int layer = 0;
        std::vector<int64_t> classCounts;
    };
    struct ScanJob
    {
        uint64_t generation = 0;
        SourceFactory factory;
        core::Rect<double> sceneRect;
        std::vector<ScanLayer> layers;   ///< largest first
    };
    struct LayerData
    {
        LayerState state;
        bool hasAnswer = false;
        bool overBudget = false;
        /// The rectangle the answer's objects cover: the grown rectangle, or
        /// the view when the grown one was over budget.
        std::optional<core::Rect<double>> covered;
    };

    void queryLoop();
    void scanLoop();
    void runScan(const ScanJob& job);
    bool current(uint64_t generation) const;
    /// Posts `apply` to the UI thread unless `generation` is stale.
    void post(uint64_t generation, std::function<void()> apply);
    void postError(uint64_t generation, const std::string& message);

    void startGeneration(std::shared_ptr<core::ResultsSource> source, std::vector<core::ResultLayerInfo> layers,
                         SourceFactory scanFactory);
    bool canReuse(const core::Rect<double>& view) const;
    void recountInView(const core::Rect<double>& view);
    void issueQuery(const core::Rect<double>& view);
    void requestDetails(const std::string& objectId);
    void startScans();
    void refreshMode(int layer);
    void clearSelectionIfHidden();
    void notifyChanged();

    Poster m_post;
    std::shared_ptr<int> m_life = std::make_shared<int>(0);  ///< posted tasks hold a weak_ptr

    // UI thread state.
    bool m_attached = false;
    SourceFactory m_factory;
    SourceFactory m_scanFactory;   ///< the source's sibling(); empty: scans use m_factory
    core::Rect<double> m_sceneRect;
    std::shared_ptr<core::ResultsSource> m_source;   ///< used by the query thread only
    std::vector<core::ResultLayerInfo> m_layers;
    std::vector<core::ResultsLayerView> m_views;
    std::vector<LayerData> m_data;
    std::map<std::string, core::Color> m_classOverrides;
    std::optional<core::Rect<double>> m_lastView;
    std::optional<core::Rect<double>> m_lastGrown;     ///< the grown rectangle last queried
    std::optional<core::Rect<double>> m_lastQueried;   ///< the view last queried
    bool m_settingsChanged = true;
    std::optional<std::string> m_selectedId;
    std::optional<core::ResultObject> m_selectedObject;
    std::optional<core::ResultDetails> m_selectedDetails;
    std::function<void()> m_onChanged;
    std::function<void(const std::string&)> m_onError;

    // Shared with the workers.
    std::atomic<uint64_t> m_generation{0};
    std::atomic<bool> m_stopping{false};
    std::mutex m_postMutex;   ///< generation bumps and posts are serialised

    std::mutex m_queryMutex;
    std::condition_variable m_queryCv;
    std::optional<QueryJob> m_queryJob;
    std::optional<DetailsJob> m_detailsJob;

    std::mutex m_scanMutex;
    std::condition_variable m_scanCv;
    std::optional<ScanJob> m_scanJob;

    std::thread m_queryThread;
    std::thread m_scanThread;
};

} // namespace slideio::viewer::app
