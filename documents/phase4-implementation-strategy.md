# Phase 4: Implementation Strategy

**Author:** C++ Developer
**Date:** 2026-03-13
**Status:** Complete
**Based on:** Phase 1 Requirements Discovery, Phase 2 UX Concept, Phase 3 System Architecture

---

## 1. C++ Standards and Conventions

### 1.1 Language Standard

**Primary standard: C++17** with selective use of C++20 features where compiler support is confirmed across all three target platforms (MSVC 2022, GCC 12+, AppleClang 14+).

**C++17 features used throughout:**

| Feature | Usage |
|---------|-------|
| `std::optional` | Return types for cache lookups, nullable results |
| `std::variant` | Annotation geometry types (Polygon, Ellipse, Arrow, etc.) |
| `std::string_view` | Non-owning string parameters in read-only APIs |
| `std::filesystem` | Cross-platform file path handling for slides, annotations, plugins |
| Structured bindings | Destructuring pairs/tuples from map/cache lookups |
| `if constexpr` | Compile-time branching in template code (serialization, geometry dispatch) |
| Nested namespaces | `namespace slideio::viewer::core { }` |
| `[[nodiscard]]` | On functions where ignoring the return value is likely a bug |
| Fold expressions | Variadic template helpers in signal/event dispatch |
| Class template argument deduction | Reducing verbosity with `std::make_unique`, `std::lock_guard` |

**C++20 features (guarded by `__cpp_*` feature test macros):**

| Feature | Usage | Fallback |
|---------|-------|----------|
| `std::jthread` | Auto-joining threads for tile workers | `std::thread` with explicit join in destructor |
| `std::format` | Log message formatting | `fmt::format` (via spdlog bundled fmt) |
| Concepts | Constraining template parameters in geometry/serialization | SFINAE or static_assert |
| Designated initializers | Struct initialization for `TileKey`, `SlideMetadata` | Constructor or aggregate init |
| `std::span` | Non-owning views over tile pixel buffers | Raw pointer + size pair |

### 1.2 Coding Style

```
Naming:
  Classes/Structs:    PascalCase          (TileCache, AnnotationModel)
  Functions/Methods:  camelCase           (readTile, setZoomLevel)
  Member variables:   m_camelCase         (m_memoryBudget, m_tileCache)
  Constants:          kPascalCase         (kDefaultTileSize, kMaxZoomLevel)
  Enums:              PascalCase::Value   (AnnotationType::Polygon)
  Namespaces:         lowercase           (slideio::viewer::core)
  Files:              PascalCase.h/.cpp   (TileCache.h, TileCache.cpp)

Formatting:
  Indent:             4 spaces (no tabs)
  Braces:             Allman style for classes/functions, K&R for control flow
  Line length:        120 characters max
  Includes:           Sorted: own header, project headers, Qt headers, std headers
  Tool:               clang-format with project .clang-format file
```

### 1.3 Header Conventions

- Every header has `#pragma once` (supported by all target compilers).
- Forward-declare where possible to minimize include chains.
- Use the PIMPL idiom for classes with complex private members exposed in public headers (especially Qt widget classes) to reduce compile times and hide implementation details.

---

## 2. Project Structure

### 2.1 Directory Layout

```
slideio-view/
  CMakeLists.txt                  # Root CMake: project definition, options, subdirectories
  conanfile.py                    # Conan package manager: dependencies
  .clang-format                   # Formatting rules
  .clang-tidy                     # Static analysis rules
  cmake/
    CompilerWarnings.cmake        # Warning flags per compiler
    Dependencies.cmake            # find_package / FetchContent calls
    Packaging.cmake               # CPack configuration
  src/
    core/                         # Domain layer: pure C++, no Qt dependency
      CMakeLists.txt
      include/slideio/viewer/core/
        Annotation.h
        AnnotationGeometry.h
        AnnotationLayer.h
        AnnotationSet.h
        Bookmark.h
        Case.h
        CoordinateSystem.h
        IAnnotationRepository.h
        ISlideSource.h
        ITileCache.h
        Measurement.h
        Slide.h
        TileData.h
        TileKey.h
        TilePyramid.h
        Types.h                   # Common typedefs, enums
        Viewport.h
      src/
        Annotation.cpp
        AnnotationGeometry.cpp
        CoordinateSystem.cpp
        Measurement.cpp
        TilePyramid.cpp
        Viewport.cpp
    app/                          # Application layer: use cases, services
      CMakeLists.txt
      include/slideio/viewer/app/
        AnnotationService.h
        BookmarkService.h
        CaseService.h
        ConfigService.h
        PluginService.h
        SlideViewerService.h
        SnapshotService.h
        UndoRedoStack.h
      src/
        AnnotationService.cpp
        BookmarkService.cpp
        CaseService.cpp
        ConfigService.cpp
        PluginService.cpp
        SlideViewerService.cpp
        SnapshotService.cpp
        UndoRedoStack.cpp
    infra/                        # Infrastructure layer: I/O, external deps
      CMakeLists.txt
      include/slideio/viewer/infra/
        AutoSaver.h
        DiskTileCache.h
        JsonAnnotationSerializer.h
        AnnotationImporter.h
        AnnotationExporter.h
        LruTileCache.h
        Prefetcher.h
        RemoteSlideClient.h
        SlideIOAdapter.h
        SlideIOAdapterPool.h
        TileDecodeWorker.h
        TileLoadScheduler.h
      src/
        AutoSaver.cpp
        DiskTileCache.cpp
        JsonAnnotationSerializer.cpp
        AnnotationImporter.cpp
        AnnotationExporter.cpp
        LruTileCache.cpp
        Prefetcher.cpp
        RemoteSlideClient.cpp
        SlideIOAdapter.cpp
        SlideIOAdapterPool.cpp
        TileDecodeWorker.cpp
        TileLoadScheduler.cpp
    ui/                           # Presentation layer: Qt widgets
      CMakeLists.txt
      include/slideio/viewer/ui/
        MainWindow.h
        ViewportWidget.h
        SplitViewController.h
        MinimapWidget.h
        ZoomIndicatorWidget.h
        SlideTrayPanel.h
        AnnotationListPanel.h
        PropertiesPanel.h
        LayerPanel.h
        MetadataPanel.h
        ToolbarManager.h
        DrawingToolController.h
        StatusBarManager.h
        ShortcutManager.h
        ThemeManager.h
        LogPanelWidget.h
      src/
        MainWindow.cpp
        ViewportWidget.cpp
        SplitViewController.cpp
        MinimapWidget.cpp
        ZoomIndicatorWidget.cpp
        SlideTrayPanel.cpp
        AnnotationListPanel.cpp
        PropertiesPanel.cpp
        LayerPanel.cpp
        MetadataPanel.cpp
        ToolbarManager.cpp
        DrawingToolController.cpp
        StatusBarManager.cpp
        ShortcutManager.cpp
        ThemeManager.cpp
        LogPanelWidget.cpp
      resources/
        icons/                    # SVG icons
        themes/                   # QSS stylesheets
        shaders/                  # GLSL vertex/fragment shaders
    main.cpp                      # Entry point
  tests/
    CMakeLists.txt
    core/                         # Unit tests for domain layer
      AnnotationGeometryTest.cpp
      CoordinateSystemTest.cpp
      TilePyramidTest.cpp
      ViewportTest.cpp
    app/                          # Unit tests for application layer
      UndoRedoStackTest.cpp
      AnnotationServiceTest.cpp
    infra/                        # Integration tests for infrastructure
      LruTileCacheTest.cpp
      JsonAnnotationSerializerTest.cpp
      SlideIOAdapterTest.cpp
    ui/                           # Widget tests
      ViewportWidgetTest.cpp
    benchmarks/
      TileCacheBenchmark.cpp
      TileDecodeBenchmark.cpp
  plugins/
    example-plugin/               # Example plugin for developers
      CMakeLists.txt
      ExamplePlugin.h
      ExamplePlugin.cpp
  docs/
    README.md
    BUILDING.md
  packaging/
    windows/                      # NSIS installer scripts
    macos/                        # DMG creation scripts
    linux/                        # AppImage/Flatpak configs
```

### 2.2 CMake Module Structure

Each `src/` subdirectory builds a separate CMake library target:

| Target | Type | Dependencies |
|--------|------|-------------|
| `slideio-viewer-core` | STATIC | None (pure C++) |
| `slideio-viewer-app` | STATIC | core, Qt6::Core |
| `slideio-viewer-infra` | STATIC | core, app, Qt6::Core, Qt6::Network, SlideIO, nlohmann_json, spdlog |
| `slideio-viewer-ui` | STATIC | core, app, infra, Qt6::Widgets, Qt6::OpenGLWidgets |
| `slideio-viewer` | EXECUTABLE | ui (links transitively to all) |

This separation enforces the layer dependency rules at build time: `core` cannot accidentally include Qt headers because it does not link against any Qt target.

---

## 3. Key Classes and Modules

### 3.1 Core Domain Classes

```cpp
// slideio/viewer/core/Annotation.h
#pragma once
#include "slideio/viewer/core/AnnotationGeometry.h"
#include "slideio/viewer/core/Types.h"
#include <chrono>
#include <string>

namespace slideio::viewer::core {

struct AnnotationProperties {
    std::string label;
    std::string classification;      // "Tumor", "Stroma", etc.
    Color color{0xE6, 0x7E, 0x22};  // default orange
    float lineWidth = 2.0f;
    float fillOpacity = 0.3f;
    std::string confidence;          // "certain", "probable", "possible"
    std::string notes;
};

struct AnnotationMetadata {
    std::string author;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point modifiedAt;
};

class Annotation {
public:
    Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry);

    [[nodiscard]] const std::string& id() const { return m_id; }
    [[nodiscard]] AnnotationType type() const { return m_type; }
    [[nodiscard]] const AnnotationGeometry& geometry() const { return m_geometry; }
    [[nodiscard]] AnnotationGeometry& geometry() { return m_geometry; }
    [[nodiscard]] const AnnotationProperties& properties() const { return m_properties; }
    [[nodiscard]] const AnnotationMetadata& metadata() const { return m_metadata; }
    [[nodiscard]] const std::string& layerId() const { return m_layerId; }

    void setProperties(AnnotationProperties props);
    void setGeometry(AnnotationGeometry geom);
    void setLayerId(std::string layerId);

    [[nodiscard]] RectF boundingBox() const;
    [[nodiscard]] bool containsPoint(PointF slidePos, double tolerance) const;
    [[nodiscard]] double area() const;       // for closed shapes
    [[nodiscard]] double perimeter() const;  // for closed shapes
    [[nodiscard]] double length() const;     // for lines/rulers

private:
    std::string m_id;
    AnnotationType m_type;
    AnnotationGeometry m_geometry;
    AnnotationProperties m_properties;
    AnnotationMetadata m_metadata;
    std::string m_layerId;
};

} // namespace slideio::viewer::core
```

```cpp
// slideio/viewer/core/AnnotationGeometry.h
#pragma once
#include "slideio/viewer/core/Types.h"
#include <variant>
#include <vector>

namespace slideio::viewer::core {

struct PolygonGeometry {
    std::vector<PointF> vertices;  // closed: first == last
};

struct EllipseGeometry {
    PointF center;
    double radiusX;
    double radiusY;
    double rotation = 0.0;  // degrees
};

struct LineStringGeometry {
    std::vector<PointF> points;
};

struct PointGeometry {
    PointF position;
};

struct ArrowGeometry {
    PointF tail;
    PointF head;
};

struct AngleGeometry {
    PointF vertex;
    PointF ray1End;
    PointF ray2End;
};

using AnnotationGeometry = std::variant<
    PolygonGeometry,
    EllipseGeometry,
    LineStringGeometry,
    PointGeometry,
    ArrowGeometry,
    AngleGeometry
>;

// Free functions operating on geometry variant
[[nodiscard]] RectF boundingBox(const AnnotationGeometry& geom);
[[nodiscard]] bool hitTest(const AnnotationGeometry& geom, PointF point, double tolerance);
[[nodiscard]] double computeArea(const AnnotationGeometry& geom);
[[nodiscard]] double computePerimeter(const AnnotationGeometry& geom);
[[nodiscard]] double computeLength(const AnnotationGeometry& geom);

} // namespace slideio::viewer::core
```

```cpp
// slideio/viewer/core/Types.h
#pragma once
#include <cstdint>
#include <string>

namespace slideio::viewer::core {

struct PointF {
    double x = 0.0;
    double y = 0.0;
};

struct SizeF {
    double width = 0.0;
    double height = 0.0;
};

struct RectF {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] bool contains(PointF p) const;
    [[nodiscard]] bool intersects(const RectF& other) const;
};

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

enum class AnnotationType {
    Rectangle,
    Ellipse,
    FreehandPolygon,
    FreehandLine,
    PointMarker,
    Arrow,
    Ruler,
    AreaMeasurement,
    AngleMeasurement,
    TextLabel
};

struct TileKey {
    std::string slideId;
    int level = 0;
    int tileX = 0;
    int tileY = 0;

    bool operator==(const TileKey& other) const = default;
};

} // namespace slideio::viewer::core

// Hash specialization for TileKey
namespace std {
template<>
struct hash<slideio::viewer::core::TileKey> {
    size_t operator()(const slideio::viewer::core::TileKey& k) const {
        size_t h = hash<string>{}(k.slideId);
        h ^= hash<int>{}(k.level) << 1;
        h ^= hash<int>{}(k.tileX) << 2;
        h ^= hash<int>{}(k.tileY) << 3;
        return h;
    }
};
} // namespace std
```

### 3.2 Application Layer: UndoRedoStack

```cpp
// slideio/viewer/app/UndoRedoStack.h
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slideio::viewer::app {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    [[nodiscard]] virtual std::string description() const = 0;
};

class UndoRedoStack {
public:
    explicit UndoRedoStack(int maxDepth = 50);

    void push(std::unique_ptr<ICommand> command);
    void undo();
    void redo();
    void clear();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] std::string undoDescription() const;
    [[nodiscard]] std::string redoDescription() const;

    // Callback for UI updates
    std::function<void()> onStackChanged;

private:
    std::vector<std::unique_ptr<ICommand>> m_commands;
    int m_currentIndex = -1;
    int m_maxDepth;
};

// Concrete command examples
class CreateAnnotationCommand : public ICommand {
public:
    CreateAnnotationCommand(class AnnotationModel* model,
                            std::unique_ptr<core::Annotation> annotation);
    void execute() override;
    void undo() override;
    [[nodiscard]] std::string description() const override;

private:
    class AnnotationModel* m_model;
    std::unique_ptr<core::Annotation> m_annotation;
    std::string m_annotationId;
};

class MoveAnnotationCommand : public ICommand {
public:
    MoveAnnotationCommand(class AnnotationModel* model,
                          std::string annotationId,
                          core::PointF oldPos, core::PointF newPos);
    void execute() override;
    void undo() override;
    [[nodiscard]] std::string description() const override;

private:
    class AnnotationModel* m_model;
    std::string m_annotationId;
    core::PointF m_oldPos;
    core::PointF m_newPos;
};

} // namespace slideio::viewer::app
```

### 3.3 Infrastructure: Tile Load Scheduler

```cpp
// slideio/viewer/infra/TileLoadScheduler.h
#pragma once
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/ITileCache.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace slideio::viewer::infra {

enum class TilePriority : int {
    Visible = 0,       // Currently visible tiles (highest priority)
    SpatialPrefetch = 1,
    DirectionalPrefetch = 2,
    ZoomPrefetch = 3,
    Background = 4
};

struct TileRequest {
    core::TileKey key;
    TilePriority priority;
    uint64_t sequenceNumber;  // for ordering within same priority

    bool operator>(const TileRequest& other) const {
        if (priority != other.priority) return priority > other.priority;
        return sequenceNumber > other.sequenceNumber;
    }
};

class TileLoadScheduler {
public:
    TileLoadScheduler(core::ITileCache& cache,
                      int workerThreadCount);
    ~TileLoadScheduler();

    void setSlideSource(std::shared_ptr<core::ISlideSource> source);
    void requestTile(const core::TileKey& key, TilePriority priority);
    void requestTiles(const std::vector<core::TileKey>& keys, TilePriority priority);
    void cancelAllPending();
    void cancelForSlide(const std::string& slideId);
    void shutdown();

    // Callback invoked on UI thread when a tile is ready
    std::function<void(const core::TileKey&)> onTileReady;

private:
    void workerLoop();

    core::ITileCache& m_cache;
    std::shared_ptr<core::ISlideSource> m_slideSource;

    std::priority_queue<TileRequest, std::vector<TileRequest>,
                        std::greater<TileRequest>> m_requestQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{true};
    std::atomic<uint64_t> m_sequenceCounter{0};
};

} // namespace slideio::viewer::infra
```

---

## 4. Qt Integration

### 4.1 QWidgets Architecture (not QML)

As decided in Phase 3, the application uses **Qt 6 Widgets** for the following reasons specific to this project:

- `QDockWidget` provides detachable, repositionable panels for multi-monitor support.
- `QOpenGLWidget` gives direct OpenGL context access for tile rendering.
- `QGraphicsView` is not used -- the viewport is a custom `QOpenGLWidget` subclass for maximum control over tile rendering.

### 4.2 ViewportWidget: Custom QOpenGLWidget

```cpp
// slideio/viewer/ui/ViewportWidget.h
#pragma once
#include "slideio/viewer/core/Viewport.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <memory>
#include <unordered_map>

namespace slideio::viewer::ui {

class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setViewportController(app::ViewportController* controller);
    void setAnnotationModel(app::AnnotationModel* model);
    void setDrawingToolController(DrawingToolController* tools);

signals:
    void cursorPositionChanged(core::PointF slidePos);
    void magnificationChanged(double magnification);
    void tileLoadingStateChanged(bool loading);

protected:
    // QOpenGLWidget overrides
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Input events
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void renderTiles();
    void renderAnnotations();
    void renderOverlays();           // minimap, zoom indicator, drawing preview
    void uploadTileTexture(const core::TileKey& key, const core::TileData& data);

    app::ViewportController* m_viewportController = nullptr;
    app::AnnotationModel* m_annotationModel = nullptr;
    DrawingToolController* m_drawingTools = nullptr;

    // OpenGL resources
    std::unique_ptr<QOpenGLShaderProgram> m_tileShader;
    std::unique_ptr<QOpenGLShaderProgram> m_annotationShader;
    GLuint m_tileVAO = 0;
    GLuint m_tileVBO = 0;

    // Tile texture cache (GPU side)
    std::unordered_map<core::TileKey, std::unique_ptr<QOpenGLTexture>> m_textures;

    // Interaction state
    bool m_isPanning = false;
    bool m_spaceHeld = false;
    QPoint m_lastMousePos;
};

} // namespace slideio::viewer::ui
```

### 4.3 Signal/Slot Patterns

The application uses Qt signal/slot connections for decoupled communication between layers:

```
TileLoadScheduler  --[tileReady signal]--> ViewportWidget::onTileReady()  --> update()
AnnotationModel    --[annotationAdded]---> AnnotationListPanel::refresh()
                   --[modelModified]-----> AutoSaver::scheduleAutoSave()
ViewportController --[viewChanged]-------> MinimapWidget::updateExtent()
                   --[viewChanged]-------> StatusBarManager::updateMagnification()
                   --[viewChanged]-------> Prefetcher::onViewportChanged()
DrawingToolController --[toolChanged]----> ToolbarManager::updateActiveToolHighlight()
                      --[toolChanged]----> StatusBarManager::updateToolHint()
```

Connection convention:
- Use `Qt::QueuedConnection` for signals crossing thread boundaries (e.g., tile worker to UI thread).
- Use `Qt::DirectConnection` (default) for same-thread signals.
- Prefer new-style `connect(&sender, &Sender::signal, &receiver, &Receiver::slot)` syntax for compile-time checking.

### 4.4 MainWindow Structure

```cpp
// slideio/viewer/ui/MainWindow.h
#pragma once
#include <QMainWindow>
#include <memory>

namespace slideio::viewer::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createActions();
    void createMenus();
    void createToolbars();
    void createDockWidgets();
    void createStatusBar();
    void connectSignals();
    void restoreLayout();
    void saveLayout();

    struct Impl;
    std::unique_ptr<Impl> m_impl;  // PIMPL for compile-time isolation
};

} // namespace slideio::viewer::ui
```

---

## 5. Threading Model

### 5.1 Thread Architecture

```
+------------------------------------------------------------------+
|                         UI Thread                                 |
|  Qt event loop, widget rendering, OpenGL calls, input handling,   |
|  annotation manipulation, signal/slot dispatch                    |
+----+----------+----------+----------+----------+---------+--------+
     |          |          |          |          |         |
     v          v          v          v          v         v
+--------+ +--------+ +--------+ +--------+ +--------+ +--------+
| Tile   | | Tile   | | Tile   | | Tile   | | Tile   | | Tile   |
| Worker | | Worker | | Worker | | Worker | | Worker | | Worker |
| #1     | | #2     | | #3     | | #4     | | #5     | | #N     |
+--------+ +--------+ +--------+ +--------+ +--------+ +--------+
     Thread pool: N = max(2, std::hardware_concurrency() - 2)

+------------------------------------------------------------------+
|                      Prefetch Thread                              |
|  Monitors viewport state, enqueues proactive tile requests        |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                     Auto-Save Thread                              |
|  Serializes annotations to JSON on timer or model-change signal   |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                   Network I/O (Qt internal)                       |
|  QNetworkAccessManager for HTTP tile fetch (remote slides only)   |
+------------------------------------------------------------------+
```

### 5.2 Thread Pool Sizing

```cpp
int workerCount() {
    int cores = static_cast<int>(std::thread::hardware_concurrency());
    // Reserve 2 cores: 1 for UI thread, 1 for prefetch/auto-save
    return std::max(2, cores - 2);
}
```

Rationale: on a typical 8-core clinical workstation, this yields 6 tile decode workers. SlideIO tile decoding is CPU-bound (JPEG/JPEG2000 decompression), so saturating available cores is appropriate.

### 5.3 Synchronization Strategy

| Shared Resource | Protection Mechanism | Rationale |
|----------------|---------------------|-----------|
| `LruTileCache` | `std::shared_mutex` (readers-writer) | Many concurrent reads (renderer, prefetcher), infrequent writes (tile insertion, eviction) |
| `TileLoadScheduler` request queue | `std::mutex` + `std::condition_variable` | Producer-consumer pattern; workers wait on the condition variable |
| `SlideIOAdapterPool` | `std::mutex` + `std::condition_variable` | Workers acquire/release adapters; blocks if pool exhausted |
| `AnnotationModel` | UI thread affinity (no locking) | All annotation mutations happen on the UI thread; worker threads never touch the model directly |
| `AutoSaver` | Takes a snapshot of annotation data on UI thread, serializes on its own thread | Avoids holding locks during file I/O |
| `ViewportController` state | UI thread affinity + atomic reads for prefetcher | Prefetcher reads a `std::atomic<ViewportState>` snapshot; never mutates |

### 5.4 Thread-to-UI Communication

Tile decode workers signal completion back to the UI thread using `QMetaObject::invokeMethod` with `Qt::QueuedConnection`:

```cpp
// In tile worker thread after decoding:
void TileDecodeWorker::onTileDecoded(const TileKey& key, TileData data) {
    m_cache.put(key, std::move(data));
    // Marshal to UI thread
    QMetaObject::invokeMethod(m_viewport, [this, key]() {
        m_viewport->onTileReady(key);
    }, Qt::QueuedConnection);
}
```

This ensures that OpenGL texture uploads and widget repaints only happen on the UI thread.

---

## 6. Memory Management

### 6.1 Ownership Model

| Object | Ownership | Mechanism |
|--------|-----------|-----------|
| `Annotation` instances | `AnnotationModel` | `std::unique_ptr<Annotation>` in a vector; model owns all annotations |
| `ISlideSource` instances | `SlideIOAdapterPool` | `std::shared_ptr`; pool manages lifecycle, workers hold temporary shared refs |
| `TileData` (pixel buffers) | `LruTileCache` | Moved into cache via `TileData` value type (owns its buffer via `std::vector<uint8_t>`) |
| GPU textures | `ViewportWidget` | `std::unique_ptr<QOpenGLTexture>` in a map; destroyed when evicted from cache or slide closed |
| Qt widgets | Parent widget (Qt object tree) | Standard Qt parent-child ownership |
| Plugin instances | `PluginService` | `std::unique_ptr<IPlugin>`; destroyed in reverse load order on shutdown |
| Commands (undo/redo) | `UndoRedoStack` | `std::unique_ptr<ICommand>` in a vector |

### 6.2 TileData Value Type

```cpp
// slideio/viewer/core/TileData.h
#pragma once
#include <cstdint>
#include <vector>

namespace slideio::viewer::core {

struct TileData {
    std::vector<uint8_t> pixels;  // RGBA, 4 bytes per pixel
    int width = 0;
    int height = 0;
    bool isError = false;         // true if tile decode failed

    [[nodiscard]] size_t byteSize() const { return pixels.size(); }
    [[nodiscard]] bool isValid() const { return !pixels.empty() && !isError; }

    // Move-only; no accidental copies of large buffers
    TileData() = default;
    TileData(TileData&&) = default;
    TileData& operator=(TileData&&) = default;
    TileData(const TileData&) = delete;
    TileData& operator=(const TileData&) = delete;
};

} // namespace slideio::viewer::core
```

### 6.3 Tile Cache Memory Budget

Memory is budgeted as follows for a single-slide session on a standard workstation:

| Component | Budget | Notes |
|-----------|--------|-------|
| Tile cache (CPU pixel buffers) | 1.0 GB | ~4096 tiles at 256KB each |
| GPU textures (mirrored) | 1.0 GB | Same tiles uploaded to GPU |
| Annotation model | < 50 MB | Even thousands of complex annotations |
| Qt widgets and framework | ~200 MB | Typical Qt 6 Widgets footprint |
| SlideIO file handles | ~100 MB | Per-slide handle pool, decompression buffers |
| Overhead | ~150 MB | Stack, misc allocations |
| **Total** | **~2.5 GB** | Well within the 4 GB target |

For multi-slide comparison (2-4 slides), the tile cache budget is shared. The `MemoryBudget` class monitors total RSS and adjusts the cache limit dynamically.

### 6.4 RAII Patterns

All resource acquisition follows RAII:

```cpp
// SlideIO adapter pool checkout/return
class AdapterLoan {
public:
    AdapterLoan(SlideIOAdapterPool& pool)
        : m_pool(pool), m_adapter(pool.acquire()) {}
    ~AdapterLoan() { m_pool.release(std::move(m_adapter)); }

    SlideIOAdapter* operator->() { return m_adapter.get(); }

    AdapterLoan(const AdapterLoan&) = delete;
    AdapterLoan& operator=(const AdapterLoan&) = delete;

private:
    SlideIOAdapterPool& m_pool;
    std::shared_ptr<SlideIOAdapter> m_adapter;
};
```

---

## 7. Performance Considerations

### 7.1 GPU Rendering Pipeline

The `ViewportWidget` uses a straightforward OpenGL 3.3 pipeline for tile rendering:

1. **Vertex shader:** Transforms tile quad vertices from slide coordinates to clip space using a single `mat4` projection matrix.
2. **Fragment shader:** Samples the tile texture with bilinear filtering. Supports alpha blending for crossfade during progressive refinement.
3. **Texture management:** Tiles are uploaded as `GL_RGBA8` textures. GPU texture memory mirrors the CPU cache. When a tile is evicted from the CPU cache, its GPU texture is also deleted.
4. **Batching:** All visible tiles at the same pyramid level are rendered in a single draw call using instanced rendering. The instance buffer contains per-tile position/UV data.

```glsl
// shaders/tile.vert
#version 330 core
layout(location = 0) in vec2 aPos;      // unit quad [0,1]x[0,1]
layout(location = 1) in vec4 aTransform; // per-instance: x, y, width, height in slide coords
layout(location = 2) in vec2 aUVOffset;  // per-instance: UV offset for atlas

uniform mat4 uProjection;

out vec2 vTexCoord;

void main() {
    vec2 worldPos = aPos * aTransform.zw + aTransform.xy;
    gl_Position = uProjection * vec4(worldPos, 0.0, 1.0);
    vTexCoord = aPos + aUVOffset;
}
```

### 7.2 Tile Prefetching Strategy

Implemented in the `Prefetcher` class:

```
Priority 0: Tiles visible in current viewport (loaded by TileLoadScheduler)
Priority 1: Ring-1 spatial neighbors (1 tile width beyond viewport, all sides)
Priority 2: Current viewport at zoom level +1 and -1
Priority 3: Directional prediction (2-3 tiles ahead of pan velocity vector)
```

The prefetcher recalculates on every `viewChanged` signal (debounced to 16ms). When the viewport changes, all pending prefetch requests with outdated viewport state are cancelled by incrementing a generation counter; workers check the counter before starting a decode.

### 7.3 Progressive Tile Refinement

When a high-resolution tile is not yet cached, the renderer:

1. Finds the nearest available lower-resolution tile that covers the same region.
2. Renders the low-res tile scaled up (bilinear filtered, appears blurry).
3. When the high-res tile arrives, crossfades from low-res to high-res over 100ms using alpha blending.

This ensures the user never sees blank/gray tiles.

### 7.4 Lazy Loading

- Slide files are opened lazily: metadata is read immediately on open, but no tiles are decoded until the viewport requests them.
- Annotation files are loaded on slide open but deserialized on the UI thread in a single batch (typically < 10ms for thousands of annotations).
- Thumbnails for the slide tray are generated on a background thread using the lowest pyramid level.

### 7.5 Profiling Strategy

| Tool | Platform | Usage |
|------|----------|-------|
| Qt Creator Profiler | All | CPU/GPU profiling during interactive use |
| Tracy Profiler | All | Frame-level profiling with zone markers in tile loading, rendering |
| Instruments (Time Profiler) | macOS | CPU hotspot analysis |
| Intel VTune | Windows/Linux | Low-level CPU analysis for tile decode bottlenecks |
| RenderDoc | All | GPU debugging, draw call analysis |
| Google Benchmark | All | Microbenchmarks for cache operations, coordinate transforms |

Key performance targets to validate:

| Metric | Target | How to Measure |
|--------|--------|----------------|
| Frame rate during pan/zoom | >= 60 fps | Tracy frame time |
| Tile decode latency (JPEG) | < 5 ms per tile | Tracy zone measurement |
| Cache lookup time | < 1 us | Google Benchmark |
| Time to first tile on slide open | < 500 ms | Manual timing + Tracy |
| Memory under single-slide load | < 3 GB | RSS monitoring |

---

## 8. Cross-Platform Challenges

### 8.1 Platform-Specific Code Isolation

All platform-specific code is isolated behind interfaces or `#ifdef` blocks in a single file per concern:

```cpp
// slideio/viewer/infra/PlatformUtils.h
#pragma once
#include <cstddef>
#include <string>
#include <filesystem>

namespace slideio::viewer::infra {

// Returns available physical RAM in bytes
size_t availableSystemMemory();

// Returns the user-specific application data directory
std::filesystem::path appDataDirectory();

// Returns the number of logical CPU cores
int logicalCoreCount();

// HiDPI scale factor for the primary screen
double primaryScreenScaleFactor();

} // namespace slideio::viewer::infra
```

Implementation uses `#ifdef _WIN32`, `#ifdef __APPLE__`, `#ifdef __linux__` in a single `.cpp` file.

### 8.2 File Paths

- All internal path handling uses `std::filesystem::path`, which handles Windows backslashes, macOS/Linux forward slashes, and Unicode paths.
- Slide file paths received from the user (file dialog, drag-and-drop) are normalized to `std::filesystem::path` at the entry point.
- Annotation file paths are derived from slide paths: `slidePath.string() + ".annotations.json"`.

### 8.3 HiDPI Support

- Qt 6 handles HiDPI natively via `Qt::AA_EnableHighDpiScaling` (enabled by default in Qt 6).
- All icon assets are SVG for resolution-independent rendering.
- The `ViewportWidget` uses `devicePixelRatio()` to render tiles at the correct density:

```cpp
void ViewportWidget::paintGL() {
    const double dpr = devicePixelRatio();
    // Render at physical pixel resolution
    glViewport(0, 0, width() * dpr, height() * dpr);
    // Tile selection uses physical viewport size for correct LOD
    auto visibleTiles = m_viewportController->visibleTileCoords(
        width() * dpr, height() * dpr);
    // ...
}
```

### 8.4 GPU Differences

| Concern | Strategy |
|---------|----------|
| OpenGL version support | Require OpenGL 3.3 Core; check at startup, fall back to software rendering if not available |
| macOS OpenGL deprecation | Qt 6 on macOS uses Metal via RHI by default; QOpenGLWidget still works through MoltenVK/Metal translation layer. Monitor Qt's RHI migration path for future switch |
| Intel integrated GPU (clinical workstations) | Keep shader complexity low; test on Intel UHD 620/630. Use `GL_RGBA8` textures (not float textures). Limit concurrent texture uploads to avoid driver stalls |
| Texture size limits | Query `GL_MAX_TEXTURE_SIZE` at startup. 256x256 tiles are well under any GPU's limit |
| VSYNC | Enable by default via `QSurfaceFormat::setSwapInterval(1)` for tear-free rendering |

### 8.5 Platform-Specific Build Notes

| Platform | Compiler | Notes |
|----------|----------|-------|
| Windows | MSVC 2022 | Use `/W4 /WX` for warnings-as-errors. Link against `opengl32.lib`. |
| macOS | AppleClang 14+ | Use `-Wall -Wextra -Werror`. Need to set `CMAKE_OSX_DEPLOYMENT_TARGET=12.0` for macOS 12+ support. Framework linking for OpenGL. |
| Linux | GCC 12+ | Use `-Wall -Wextra -Werror`. Link against `libGL`. May need Mesa for OpenGL on headless CI. |

---

## 9. Build System

### 9.1 Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
project(slideio-viewer
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Options
option(SLIDEIO_VIEWER_BUILD_TESTS "Build unit tests" ON)
option(SLIDEIO_VIEWER_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(SLIDEIO_VIEWER_ENABLE_ASAN "Enable AddressSanitizer" OFF)

# Compiler warnings
include(cmake/CompilerWarnings.cmake)

# Dependencies
include(cmake/Dependencies.cmake)

# Sub-projects (dependency order)
add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(src/infra)
add_subdirectory(src/ui)

# Main executable
add_executable(slideio-viewer src/main.cpp)
target_link_libraries(slideio-viewer PRIVATE slideio-viewer-ui)

# Tests
if(SLIDEIO_VIEWER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Packaging
include(cmake/Packaging.cmake)
```

### 9.2 Dependency Management (Conan)

```python
# conanfile.py
from conan import ConanFile
from conan.tools.cmake import cmake_layout

class SlideioViewerConan(ConanFile):
    name = "slideio-viewer"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("qt/[>=6.5.0]")
        self.requires("nlohmann_json/[>=3.11.0]")
        self.requires("spdlog/[>=1.12.0]")
        self.requires("catch2/[>=3.4.0]")
        self.requires("benchmark/[>=1.8.0]")

    def layout(self):
        cmake_layout(self)
```

SlideIO is not in Conan; it is brought in via `FetchContent` or as a pre-built external package:

```cmake
# cmake/Dependencies.cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets Network)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(slideio REQUIRED)  # Pre-installed or FetchContent

if(SLIDEIO_VIEWER_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED)
endif()

if(SLIDEIO_VIEWER_BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
endif()
```

### 9.3 CI/CD Pipeline

```yaml
# .github/workflows/ci.yml (conceptual)
matrix:
  os: [windows-latest, macos-latest, ubuntu-22.04]
  build_type: [Debug, Release]

steps:
  - checkout
  - install Conan dependencies (conan install .)
  - cmake configure with -DSLIDEIO_VIEWER_BUILD_TESTS=ON
  - cmake build
  - run ctest
  - (Release only) create package with CPack
  - upload artifacts
```

Key CI checks:
- Build on all three platforms with warnings-as-errors.
- Run all unit tests.
- Run AddressSanitizer builds on Linux/macOS for memory safety.
- Run clang-tidy for static analysis.
- Run clang-format check for style compliance.

---

## 10. Testing Strategy

### 10.1 Unit Tests (Core and App layers)

Target: **high coverage** of the domain layer, which contains pure C++ logic with no external dependencies.

```cpp
// tests/core/AnnotationGeometryTest.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "slideio/viewer/core/AnnotationGeometry.h"

using namespace slideio::viewer::core;
using Catch::Approx;

TEST_CASE("PolygonGeometry bounding box", "[geometry]") {
    PolygonGeometry poly;
    poly.vertices = {{10, 20}, {50, 20}, {50, 60}, {10, 60}, {10, 20}};

    auto bbox = boundingBox(AnnotationGeometry{poly});
    CHECK(bbox.x == 10.0);
    CHECK(bbox.y == 20.0);
    CHECK(bbox.width == 40.0);
    CHECK(bbox.height == 40.0);
}

TEST_CASE("PolygonGeometry area calculation", "[geometry]") {
    // 40x40 rectangle = 1600 sq units
    PolygonGeometry poly;
    poly.vertices = {{0, 0}, {40, 0}, {40, 40}, {0, 40}, {0, 0}};

    CHECK(computeArea(AnnotationGeometry{poly}) == Approx(1600.0));
}

TEST_CASE("EllipseGeometry hit test", "[geometry]") {
    EllipseGeometry ellipse{.center = {100, 100}, .radiusX = 50, .radiusY = 30};

    CHECK(hitTest(AnnotationGeometry{ellipse}, {100, 100}, 0.0) == true);   // center
    CHECK(hitTest(AnnotationGeometry{ellipse}, {150, 100}, 1.0) == true);   // edge
    CHECK(hitTest(AnnotationGeometry{ellipse}, {200, 200}, 0.0) == false);  // outside
}

TEST_CASE("UndoRedoStack operations", "[app]") {
    UndoRedoStack stack(10);

    // ... mock annotation model
    CHECK(stack.canUndo() == false);
    CHECK(stack.canRedo() == false);

    // Push a command, undo it, redo it
    // ...
}
```

### 10.2 Integration Tests (Infrastructure layer)

Test the infrastructure components with real or mock SlideIO:

```cpp
TEST_CASE("LruTileCache respects memory budget", "[cache]") {
    LruTileCache cache(1024 * 1024);  // 1 MB budget

    TileKey key1{"slide1", 0, 0, 0};
    TileData data1;
    data1.pixels.resize(256 * 256 * 4);  // 256 KB
    data1.width = 256;
    data1.height = 256;

    cache.put(key1, std::move(data1));
    CHECK(cache.get(key1).has_value());
    CHECK(cache.memoryUsage() == 256 * 256 * 4);

    // Fill cache beyond budget -> LRU eviction
    for (int i = 1; i <= 5; ++i) {
        TileKey key{"slide1", 0, i, 0};
        TileData data;
        data.pixels.resize(256 * 256 * 4);
        data.width = 256;
        data.height = 256;
        cache.put(key, std::move(data));
    }

    CHECK(cache.memoryUsage() <= 1024 * 1024);
    // Oldest tile should have been evicted
    CHECK(!cache.get(key1).has_value());
}
```

### 10.3 JSON Serialization Round-Trip Tests

```cpp
TEST_CASE("Annotation JSON round-trip", "[serialization]") {
    // Create annotation set with various types
    AnnotationSet original;
    // ... add polygon, ellipse, point, ruler annotations

    JsonAnnotationSerializer serializer;
    std::string json = serializer.serialize(original);
    AnnotationSet restored = serializer.deserialize(json);

    CHECK(restored.annotations.size() == original.annotations.size());
    // Verify geometry, properties, metadata match
}
```

### 10.4 Visual Regression Tests

For the viewport rendering, use a headless OpenGL context (via `QOffscreenSurface`) to render a known slide region and compare the output image against a reference:

```cpp
TEST_CASE("Viewport renders tiles correctly", "[visual]") {
    // Create offscreen GL context
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext context;
    context.create();
    context.makeCurrent(&surface);

    // Render a known tile arrangement
    // Capture framebuffer
    // Compare against reference image with per-pixel tolerance
}
```

Visual regression tests are run on CI with Mesa software rendering for deterministic output.

### 10.5 Test Organization

| Test Category | Framework | Runs On | Purpose |
|--------------|-----------|---------|---------|
| Unit (core) | Catch2 | Every commit | Domain logic correctness |
| Unit (app) | Catch2 | Every commit | Service/command correctness |
| Integration (infra) | Catch2 | Every commit | Cache, serialization, adapter correctness |
| Integration (SlideIO) | Catch2 | Nightly (needs slide files) | Real slide loading, tile decode correctness |
| Visual regression | Catch2 + image diff | Nightly | Rendering correctness |
| Performance | Google Benchmark | Weekly / on-demand | Performance regression detection |

---

## 11. Example Class Structures

### 11.1 ViewportController (Application Layer)

```cpp
// slideio/viewer/app/ViewportController.h
#pragma once
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/core/TilePyramid.h"
#include <QObject>
#include <vector>
#include <atomic>

namespace slideio::viewer::app {

struct ViewportState {
    core::PointF center;          // slide coordinates of viewport center
    double magnification = 1.0;   // objective magnification equivalent
    double micronsPerPixel = 1.0;
    int viewportWidth = 0;        // screen pixels
    int viewportHeight = 0;
};

class ViewportController : public QObject {
    Q_OBJECT

public:
    explicit ViewportController(QObject* parent = nullptr);

    void setSlide(const core::TilePyramid& pyramid, double slideResolution);
    void setViewportSize(int width, int height);

    // Coordinate transforms
    [[nodiscard]] core::PointF screenToSlide(core::PointF screenPos) const;
    [[nodiscard]] core::PointF slideToScreen(core::PointF slidePos) const;
    [[nodiscard]] core::RectF visibleSlideRect() const;

    // Navigation
    void setCenter(core::PointF slidePos);
    void setMagnification(double magnification);
    void zoomAtScreenPoint(core::PointF screenPos, double factor);
    void fitToScreen();
    void panByScreenDelta(core::PointF delta);

    // Tile queries
    [[nodiscard]] int currentPyramidLevel() const;
    [[nodiscard]] std::vector<core::TileKey> visibleTileCoords(
        const std::string& slideId) const;
    [[nodiscard]] std::vector<core::TileKey> prefetchTileCoords(
        const std::string& slideId) const;

    // State access
    [[nodiscard]] double currentMagnification() const { return m_state.magnification; }
    [[nodiscard]] double currentMicronsPerPixel() const { return m_state.micronsPerPixel; }
    [[nodiscard]] ViewportState state() const { return m_state; }

signals:
    void viewChanged();
    void magnificationChanged(double magnification);

private:
    void clampToSlideBounds();
    void updateDerivedState();

    ViewportState m_state;
    core::TilePyramid m_pyramid;
    double m_slideResolution = 0.25;  // um/px at level 0
    double m_minMagnification = 0.25;
    double m_maxMagnification = 40.0;
};

} // namespace slideio::viewer::app
```

### 11.2 AnnotationModel (Application Layer)

```cpp
// slideio/viewer/app/AnnotationModel.h
#pragma once
#include "slideio/viewer/core/Annotation.h"
#include "slideio/viewer/core/AnnotationLayer.h"
#include <QObject>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace slideio::viewer::app {

class AnnotationModel : public QObject {
    Q_OBJECT

public:
    explicit AnnotationModel(QObject* parent = nullptr);

    // CRUD
    void addAnnotation(std::unique_ptr<core::Annotation> annotation);
    void removeAnnotation(const std::string& id);
    void updateProperties(const std::string& id, core::AnnotationProperties props);
    void updateGeometry(const std::string& id, core::AnnotationGeometry geom);
    void moveAnnotation(const std::string& id, core::PointF delta);

    // Query
    [[nodiscard]] core::Annotation* findById(const std::string& id) const;
    [[nodiscard]] std::vector<core::Annotation*> findInRect(core::RectF rect) const;
    [[nodiscard]] std::vector<core::Annotation*> findByLayer(const std::string& layerId) const;
    [[nodiscard]] core::Annotation* hitTest(core::PointF slidePos, double tolerance) const;
    [[nodiscard]] std::vector<core::Annotation*> allAnnotations() const;

    // Layer management
    void addLayer(const std::string& name);
    void removeLayer(const std::string& id);
    void setLayerVisible(const std::string& id, bool visible);
    void setLayerLocked(const std::string& id, bool locked);
    void setLayerOpacity(const std::string& id, float opacity);
    [[nodiscard]] std::vector<core::AnnotationLayer> layers() const;
    [[nodiscard]] std::string activeLayerId() const { return m_activeLayerId; }
    void setActiveLayerId(const std::string& id);

    // Serialization
    [[nodiscard]] core::AnnotationSet toAnnotationSet() const;
    void fromAnnotationSet(const core::AnnotationSet& set);
    void clear();

signals:
    void annotationAdded(const std::string& id);
    void annotationRemoved(const std::string& id);
    void annotationChanged(const std::string& id);
    void layerChanged(const std::string& layerId);
    void modelModified();

private:
    std::vector<std::unique_ptr<core::Annotation>> m_annotations;
    std::unordered_map<std::string, core::Annotation*> m_idIndex;
    std::vector<core::AnnotationLayer> m_layers;
    std::string m_activeLayerId;
};

} // namespace slideio::viewer::app
```

### 11.3 LruTileCache (Infrastructure Layer)

```cpp
// slideio/viewer/infra/LruTileCache.h
#pragma once
#include "slideio/viewer/core/ITileCache.h"
#include <list>
#include <shared_mutex>
#include <unordered_map>

namespace slideio::viewer::infra {

class LruTileCache : public core::ITileCache {
public:
    explicit LruTileCache(size_t memoryBudget);

    std::optional<core::TileData> get(const core::TileKey& key) override;
    void put(const core::TileKey& key, core::TileData data) override;
    void evict(const std::string& slideId) override;
    [[nodiscard]] size_t memoryUsage() const override;
    void setMemoryBudget(size_t bytes) override;

    [[nodiscard]] size_t tileCount() const;
    [[nodiscard]] double hitRate() const;
    void resetStats();

private:
    void evictToFitBudget();

    struct Entry {
        core::TileKey key;
        core::TileData data;
        size_t byteSize;
    };

    std::unordered_map<core::TileKey, std::list<Entry>::iterator> m_map;
    std::list<Entry> m_lruList;  // front = most recent
    size_t m_memoryBudget;
    size_t m_currentUsage = 0;
    mutable std::shared_mutex m_mutex;

    // Stats
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
};

} // namespace slideio::viewer::infra
```

### 11.4 DrawingToolController (UI Layer)

```cpp
// slideio/viewer/ui/DrawingToolController.h
#pragma once
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/core/AnnotationGeometry.h"
#include <QObject>
#include <memory>
#include <vector>

namespace slideio::viewer::ui {

enum class ActiveTool {
    Select,
    Pan,
    Rectangle,
    Ellipse,
    FreehandPolygon,
    PointMarker,
    Arrow,
    Ruler,
    AreaMeasurement,
    AngleMeasurement,
    TextLabel
};

class DrawingToolController : public QObject {
    Q_OBJECT

public:
    explicit DrawingToolController(QObject* parent = nullptr);

    void setActiveTool(ActiveTool tool);
    [[nodiscard]] ActiveTool activeTool() const { return m_activeTool; }

    // Input events forwarded from ViewportWidget (in slide coordinates)
    void onMousePress(core::PointF slidePos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void onMouseMove(core::PointF slidePos, Qt::KeyboardModifiers mods);
    void onMouseRelease(core::PointF slidePos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void onKeyPress(int key, Qt::KeyboardModifiers mods);
    void onDoubleClick(core::PointF slidePos);

    // Preview geometry for rendering during creation
    [[nodiscard]] bool hasPreview() const { return m_isDrawing; }
    [[nodiscard]] const core::AnnotationGeometry& previewGeometry() const { return m_preview; }

    void cancel();

signals:
    void toolChanged(ActiveTool tool);
    void annotationCreated(core::AnnotationType type, core::AnnotationGeometry geometry);
    void previewUpdated();

private:
    void handleRectangleInput(core::PointF slidePos, Qt::KeyboardModifiers mods);
    void handleFreehandInput(core::PointF slidePos, Qt::KeyboardModifiers mods);
    void handlePointInput(core::PointF slidePos);
    void handleRulerInput(core::PointF slidePos, Qt::KeyboardModifiers mods);
    // ... handlers for each tool

    ActiveTool m_activeTool = ActiveTool::Select;
    bool m_isDrawing = false;
    core::AnnotationGeometry m_preview;
    std::vector<core::PointF> m_currentVertices;
    core::PointF m_startPos;
    bool m_constrainProportions = false;  // Shift held
};

} // namespace slideio::viewer::ui
```

### 11.5 SlideIOAdapter (Infrastructure Layer)

```cpp
// slideio/viewer/infra/SlideIOAdapter.h
#pragma once
#include "slideio/viewer/core/ISlideSource.h"
#include <memory>
#include <mutex>
#include <string>

// Forward declare SlideIO types to avoid leaking headers
namespace slideio {
class Slide;
class Scene;
}

namespace slideio::viewer::infra {

class SlideIOAdapter : public core::ISlideSource {
public:
    explicit SlideIOAdapter(const std::string& filePath);
    ~SlideIOAdapter() override;

    [[nodiscard]] core::SlideMetadata metadata() const override;
    [[nodiscard]] int pyramidLevelCount() const override;
    [[nodiscard]] core::SizeI levelDimensions(int level) const override;
    [[nodiscard]] double resolution() const override;
    [[nodiscard]] int channelCount() const override;

    core::TileData readTile(int level, int tileX, int tileY,
                            int tileWidth, int tileHeight) const override;

    QImage thumbnail(int maxWidth, int maxHeight) const override;
    void close() override;

private:
    std::shared_ptr<::slideio::Slide> m_slide;
    std::shared_ptr<::slideio::Scene> m_scene;
    core::SlideMetadata m_metadata;
    mutable std::mutex m_readMutex;
};

} // namespace slideio::viewer::infra
```

### 11.6 AutoSaver (Infrastructure Layer)

```cpp
// slideio/viewer/infra/AutoSaver.h
#pragma once
#include "slideio/viewer/core/IAnnotationRepository.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace slideio::viewer::infra {

class AutoSaver {
public:
    AutoSaver(core::IAnnotationRepository& repository,
              std::chrono::milliseconds debounceInterval = std::chrono::milliseconds{2000},
              std::chrono::milliseconds periodicInterval = std::chrono::seconds{30});
    ~AutoSaver();

    // Called from UI thread when model changes
    void scheduleAutoSave(const std::string& slideId,
                          std::function<core::AnnotationSet()> snapshotFn);

    void shutdown();

private:
    void saveLoop();

    core::IAnnotationRepository& m_repository;
    std::chrono::milliseconds m_debounceInterval;
    std::chrono::milliseconds m_periodicInterval;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{true};

    // Pending save state
    std::string m_pendingSlideId;
    std::function<core::AnnotationSet()> m_pendingSnapshot;
    std::chrono::steady_clock::time_point m_lastChangeTime;
    bool m_hasPendingChange = false;
};

} // namespace slideio::viewer::infra
```

---

## 12. Implementation Phases (Suggested Order)

### Phase A: Foundation (Weeks 1-3)

1. Project scaffolding: CMake structure, Conan setup, CI pipeline.
2. Core domain types: `Types.h`, `TileKey`, `TileData`, `TilePyramid`, `Viewport`.
3. `SlideIOAdapter`: open slide, read metadata, decode tiles.
4. `LruTileCache`: insert, lookup, evict, memory budget.
5. Basic `ViewportWidget`: render tiles from cache with OpenGL.
6. Basic `MainWindow`: window with viewport, file open dialog.

**Milestone:** Open a local SVS file and display the lowest resolution level.

### Phase B: Navigation (Weeks 4-5)

1. `ViewportController`: coordinate transforms, zoom, pan, fit-to-screen.
2. Mouse/keyboard input handling in `ViewportWidget`.
3. `TileLoadScheduler` with thread pool.
4. Progressive tile refinement (low-res to high-res crossfade).
5. `Prefetcher` for spatial and zoom prefetching.
6. `MinimapWidget` and `ZoomIndicatorWidget`.

**Milestone:** Smooth 60fps pan/zoom on gigapixel slides.

### Phase C: Annotations (Weeks 6-8)

1. `Annotation` and `AnnotationGeometry` domain classes.
2. `AnnotationModel` with CRUD and signals.
3. `DrawingToolController` for all annotation types.
4. Annotation rendering overlay in `ViewportWidget`.
5. `UndoRedoStack` with command pattern.
6. `AnnotationListPanel`, `PropertiesPanel`, `LayerPanel`.
7. `JsonAnnotationSerializer` with import/export.
8. `AutoSaver`.

**Milestone:** Full annotation workflow: create, edit, undo, save, reload.

### Phase D: Case Management and UI Polish (Weeks 9-10)

1. `CaseService` and case model.
2. `SlideTrayPanel` with thumbnails.
3. `SplitViewController` for multi-slide comparison.
4. `BookmarkService` and navigation history.
5. `SnapshotService` for viewport capture.
6. `ThemeManager` (light/dark themes).
7. `ShortcutManager` with full keyboard shortcut scheme.
8. `StatusBarManager`.

**Milestone:** Complete clinical workflow: open case, review slides, annotate, compare, export.

### Phase E: Advanced Features (Weeks 11-13)

1. Remote slide access (`RemoteSlideClient`, `DiskTileCache`).
2. Multi-monitor support (detachable dock widgets, saved layouts).
3. Plugin system (`PluginLoader`, `PluginContext`, example plugin).
4. Measurement tools with calibrated units.
5. Session recovery.
6. Accessibility improvements.

**Milestone:** Feature-complete application ready for testing.

### Phase F: Testing and Optimization (Weeks 14-16)

1. Comprehensive unit and integration test suite.
2. Visual regression test infrastructure.
3. Performance profiling and optimization passes.
4. Cross-platform testing and bug fixes.
5. Packaging (NSIS, DMG, AppImage).
6. Documentation.

**Milestone:** Release candidate.

---

*This document serves as input for Phase 5 (Critical Review) and ultimately feeds into Document 3 (Software Architecture and Design).*
