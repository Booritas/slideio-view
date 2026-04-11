# Phase 3: System Architecture

**Author:** Technical Architect
**Date:** 2026-03-13
**Status:** Complete
**Based on:** Phase 1 Requirements Discovery, Phase 2 UX Concept

---

## 1. System Overview

SlideIO Viewer is a cross-platform desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It targets clinical pathologists, researchers, lab technicians, and trainees who need to review gigapixel-scale tissue images with sub-second responsiveness.

The application wraps the SlideIO library for format-agnostic slide access, provides GPU-accelerated tile rendering through Qt 6, and stores annotations in a portable JSON-based format independent of the slide files. It supports local and remote slide access, multi-slide comparison, and a plugin system for future extensibility.

**Key architectural drivers:**

- **Performance:** 60fps pan/zoom on gigapixel images with a 4GB memory budget.
- **Format neutrality:** All slide format details are encapsulated behind SlideIO; the application never handles format-specific logic.
- **Annotation independence:** Annotations are stored separately from slides, versioned, and exportable.
- **Cross-platform:** Windows, macOS, and Linux via Qt 6 and C++17/20.
- **Extensibility:** Plugin architecture for AI analysis, custom annotation types, and institutional integrations.

---

## 2. Architectural Style

The application follows a **layered architecture** with strict dependency direction: upper layers depend on lower layers, never the reverse. Cross-cutting concerns (logging, configuration, error handling) are accessible to all layers via dependency injection.

```
+------------------------------------------------------------------+
|                    Presentation Layer (Qt UI)                     |
|  MainWindow, ViewportWidget, Panels, Toolbars, Dialogs           |
+------------------------------------------------------------------+
|                    Application Layer (Use Cases)                  |
|  SlideViewerService, AnnotationService, CaseService,             |
|  BookmarkService, SnapshotService, PluginService                 |
+------------------------------------------------------------------+
|                    Domain Layer (Core Logic)                      |
|  Slide, Annotation, Case, Layer, Bookmark, Measurement,          |
|  TilePyramid, Viewport, CoordinateSystem                         |
+------------------------------------------------------------------+
|                    Infrastructure Layer                           |
|  SlideIOAdapter, TileCache, TileLoader, RemoteClient,            |
|  AnnotationStore, ConfigStore, PluginLoader                      |
+------------------------------------------------------------------+
```

### Layer responsibilities

| Layer | Responsibility | Dependencies |
|-------|---------------|--------------|
| **Presentation** | Qt widgets, event handling, rendering, user input | Application |
| **Application** | Orchestrates use cases, coordinates domain objects, enforces workflows | Domain |
| **Domain** | Core business entities and rules, coordinate math, annotation geometry | None (pure C++) |
| **Infrastructure** | External I/O: SlideIO, file system, network, caching, plugin loading | Domain (interfaces) |

The Domain layer defines abstract interfaces (e.g., `ISlideSource`, `ITileCache`, `IAnnotationRepository`) that the Infrastructure layer implements. The Application layer wires them together. This inversion of control makes every layer independently testable.

---

## 3. Major Components

### 3.1 Component Overview

```
+-------------------+     +-------------------+     +-------------------+
|   Viewer Engine   |     | Annotation Engine |     |   Case Manager    |
|   - Viewport      |     | - Drawing tools   |     |   - Case model    |
|   - Tile renderer |<--->| - Geometry engine |<--->|   - Slide list    |
|   - Zoom/pan ctrl |     | - Layer manager   |     |   - Metadata      |
|   - Split view    |     | - Undo/redo stack |     |   - Bookmarks     |
+--------+----------+     +---------+---------+     +-------------------+
         |                          |
         v                          v
+-------------------+     +-------------------+     +-------------------+
|   Cache Manager   |     | Annotation Store  |     |  Plugin System    |
|   - LRU tile cache|     | - JSON serializer |     |  - Plugin loader  |
|   - Memory budget |     | - Import/export   |     |  - Extension API  |
|   - Prefetcher    |     | - Version control |     |  - Hook points    |
+--------+----------+     +-------------------+     +-------------------+
         |
         v
+-------------------+     +-------------------+
|   Slide Manager   |     | Remote Access     |
|   - SlideIO wrap  |     | - HTTP client     |
|   - Tile decoder  |     | - Tile streaming  |
|   - Metadata read |     | - Local disk cache|
+-------------------+     +-------------------+
```

### 3.2 Viewer Engine

Responsible for rendering slide tiles, managing the viewport, and handling navigation input.

- **ViewportController:** Manages the current view rectangle in slide coordinates, zoom level, and coordinate transforms between slide space, screen space, and tile space.
- **TileRenderer:** Renders visible tiles using OpenGL (via Qt's QOpenGLWidget). Handles progressive refinement: draws lower-resolution tiles as placeholders, crossfades to full-resolution tiles when available.
- **SplitViewController:** Manages 1-4 viewports in a grid layout. Handles synchronized navigation (linked pan/zoom) across panels.
- **NavigationHistory:** Maintains a stack of viewport states for back/forward navigation.

### 3.3 Annotation Engine

Manages all annotation-related functionality.

- **AnnotationModel:** The in-memory model of all annotations on a slide, organized by layer. Provides signals for changes.
- **GeometryEngine:** Hit testing, bounding box calculation, area/perimeter/distance computation, vertex manipulation, and coordinate transforms for annotation geometry.
- **DrawingToolController:** Manages active drawing tool state, handles mouse/keyboard input during annotation creation, and produces annotation objects.
- **UndoRedoStack:** Per-slide command stack implementing the Command pattern. Each annotation operation is a reversible command.
- **LayerManager:** Manages annotation layers (visibility, lock state, opacity, ordering).

### 3.4 Case Manager

Manages the logical grouping of slides into cases with metadata.

- **CaseModel:** A case containing ordered slides with metadata (patient ID, accession number, clinical history).
- **SlideList:** Ordered collection of slide references with thumbnails, stain labels, and quality flags.
- **BookmarkManager:** Named viewport states associated with a case/slide, including quick-bookmark slots.

### 3.5 Cache Manager

Manages the in-memory tile cache and prefetching.

- **TileCache:** Thread-safe LRU cache mapping (slide_id, level, tile_x, tile_y) to decoded pixel buffers and GPU textures.
- **MemoryBudget:** Monitors total cache memory usage and triggers eviction when approaching the configured limit.
- **Prefetcher:** Background thread that requests tiles adjacent to the viewport and at neighboring zoom levels. Uses directional prediction based on pan velocity.

### 3.6 Slide Manager

Wraps SlideIO for slide access.

- **SlideIOAdapter:** Implements the `ISlideSource` interface using SlideIO. Handles opening slides, reading metadata, and decoding tiles.
- **TileDecoder:** Decodes tile regions from slide files on background threads. Converts raw pixel data to RGBA buffers suitable for GPU upload.
- **MetadataReader:** Extracts scanner metadata, resolution, dimensions, channel info, and Z-stack information from slide files via SlideIO.

### 3.7 Remote Access Layer

Handles accessing slides over HTTP/HTTPS.

- **RemoteSlideClient:** HTTP client that fetches tile data from a REST tile server. Implements the same `ISlideSource` interface as the local adapter.
- **DiskTileCache:** Persistent on-disk cache for remotely fetched tiles, avoiding re-download across sessions.
- **ConnectionManager:** Manages connection state, retry logic, and bandwidth monitoring for remote sources.

### 3.8 Annotation Store

Handles persistence of annotations.

- **JsonAnnotationSerializer:** Serializes/deserializes annotation models to/from JSON files.
- **AnnotationImporter:** Imports annotations from external formats (QuPath GeoJSON, ASAP XML).
- **AnnotationExporter:** Exports annotations to GeoJSON, CSV (measurements), and custom formats.
- **AutoSaver:** Debounced auto-save (2s after last change, plus periodic 30s saves).

### 3.9 Plugin System

Provides extension points for third-party functionality.

- **PluginLoader:** Discovers and loads shared library plugins from a configured directory.
- **PluginAPI:** Stable C++ API that plugins link against, providing access to the slide, annotations, viewport, and UI extension points.
- **HookRegistry:** Registry of named extension points where plugins register callbacks.

---

## 4. Component Diagram

### 4.1 High-Level Component Dependencies

```
                        +------------------+
                        |   Qt UI Layer    |
                        | (Presentation)   |
                        +--------+---------+
                                 |
                    +------------+------------+
                    |                         |
           +--------v--------+      +--------v--------+
           | Viewer Engine   |      | Annotation      |
           | (Application)   |      | Engine (App)    |
           +--------+--------+      +--------+--------+
                    |                         |
         +----------+----------+    +---------+---------+
         |          |          |    |         |         |
   +-----v---+ +---v-----+ +--v----v--+ +----v----+ +--v--------+
   | Cache    | | Slide   | | Domain   | | Annot.  | | Plugin    |
   | Manager  | | Manager | | Model    | | Store   | | System    |
   +-----+----+ +----+----+ +----------+ +---------+ +-----------+
         |            |
   +-----v----+ +----v-------+
   | Memory   | | SlideIO    |
   | Allocator| | (External) |
   +----------+ +----+-------+
                     |
              +------v-------+
              | Remote Access|
              | Layer        |
              +--------------+
```

### 4.2 Threading Model Overview

```
+------------------------------------------------------------------+
|                         UI Thread                                 |
|  - Event loop, Qt signals/slots                                  |
|  - Viewport rendering (OpenGL)                                   |
|  - Annotation drawing                                            |
|  - Input handling                                                |
+------------------------------------------------------------------+
        |               |                |              |
        v               v                v              v
+-------------+ +-------------+ +-------------+ +-------------+
| Tile Decode | | Tile Decode | | Tile Decode | | Tile Decode |
| Worker #1   | | Worker #2   | | Worker #3   | | Worker #N   |
+-------------+ +-------------+ +-------------+ +-------------+
        (Thread pool: std::hardware_concurrency() - 1 threads)

+------------------------------------------------------------------+
|                      Prefetch Thread                              |
|  - Monitors viewport state                                       |
|  - Enqueues tile decode requests for adjacent tiles               |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                     Auto-Save Thread                              |
|  - Periodic annotation serialization                             |
|  - Debounced save on annotation change                           |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                   Network I/O Thread(s)                           |
|  - HTTP tile fetch for remote slides                             |
|  - Runs in Qt's QNetworkAccessManager thread                     |
+------------------------------------------------------------------+
```

---

## 5. Data Flow

### 5.1 Tile Loading Pipeline

```
User pans/zooms viewport
        |
        v
ViewportController calculates visible tile coordinates
  (slide_id, pyramid_level, tile_col, tile_row)
        |
        v
TileCache lookup  ----[HIT]----> TileRenderer uploads to GPU texture
        |                                    |
     [MISS]                                  v
        |                              Display on screen
        v
TileDecoder enqueues request to thread pool
        |
        v
SlideIOAdapter.readTile(slide_id, level, x, y, w, h)
        |
        v
SlideIO library reads compressed tile from file/network
        |
        v
Decoded RGBA pixel buffer returned to calling thread
        |
        v
TileCache stores decoded buffer (LRU eviction if needed)
        |
        v
Signal emitted to UI thread: tile ready
        |
        v
TileRenderer uploads new tile to GPU, crossfade from placeholder
        |
        v
Display sharp tile on screen
```

### 5.2 Annotation Creation Flow

```
User selects drawing tool (e.g., presses R for Rectangle)
        |
        v
DrawingToolController enters Rectangle mode
        |
        v
User clicks and drags on viewport
        |
        v
ViewportWidget captures mouse events, translates screen coords
  to slide coords via ViewportController.screenToSlide()
        |
        v
DrawingToolController accumulates geometry in slide coordinates
  (displays real-time preview via overlay rendering)
        |
        v
User releases mouse -> annotation creation complete
        |
        v
DrawingToolController creates Annotation domain object
  with geometry, default properties, active layer, author, timestamp
        |
        v
UndoRedoStack records CreateAnnotationCommand
        |
        v
AnnotationModel.addAnnotation() inserts into model
        |
        v
Signal: annotationAdded -> ViewportWidget repaints annotation overlay
                        -> Right panel updates annotation list
                        -> AutoSaver schedules debounced save
```

### 5.3 Slide Open Flow

```
User opens slide (File > Open, drag-and-drop, or case panel click)
        |
        v
SlideViewerService.openSlide(path_or_url)
        |
        v
Determine source type: local file vs. remote URL
        |
        +--[local]----> SlideIOAdapter.open(path)
        |                      |
        +--[remote]---> RemoteSlideClient.connect(url)
                               |
                               v
                        ISlideSource instance created
                               |
                               v
MetadataReader extracts: dimensions, resolution (um/px),
  pyramid levels, channel count, scanner info
        |
        v
TilePyramid model constructed: level count, tile size,
  dimensions at each level
        |
        v
ViewportController initializes: sets initial view to
  fit-to-screen with 5% margin
        |
        v
TileRenderer requests tiles for initial viewport
        |
        v
(Tile loading pipeline from 5.1 executes)
        |
        v
AnnotationStore.loadAnnotations(slide_id)
  loads any existing annotation file for this slide
        |
        v
Slide displayed with annotations overlaid
  Total target: <2s for local, <5s for remote
```

---

## 6. API Boundaries

### 6.1 ISlideSource (Domain Interface)

The core abstraction for slide access, implemented by both `SlideIOAdapter` (local) and `RemoteSlideClient` (remote).

```cpp
class ISlideSource {
public:
    virtual ~ISlideSource() = default;

    // Slide metadata
    virtual SlideMetadata metadata() const = 0;
    virtual int pyramidLevelCount() const = 0;
    virtual QSize levelDimensions(int level) const = 0;
    virtual double resolution() const = 0;  // microns per pixel at level 0
    virtual int channelCount() const = 0;

    // Tile access (called from worker threads)
    virtual TileData readTile(int level, int tileX, int tileY,
                              int tileWidth, int tileHeight) const = 0;

    // Thumbnail
    virtual QImage thumbnail(int maxWidth, int maxHeight) const = 0;

    // Lifecycle
    virtual void close() = 0;
};
```

### 6.2 ITileCache (Domain Interface)

```cpp
class ITileCache {
public:
    virtual ~ITileCache() = default;

    struct TileKey {
        std::string slideId;
        int level;
        int tileX;
        int tileY;
    };

    virtual std::optional<TileData> get(const TileKey& key) = 0;
    virtual void put(const TileKey& key, TileData data) = 0;
    virtual void evict(const std::string& slideId) = 0;
    virtual size_t memoryUsage() const = 0;
    virtual void setMemoryBudget(size_t bytes) = 0;
};
```

### 6.3 IAnnotationRepository (Domain Interface)

```cpp
class IAnnotationRepository {
public:
    virtual ~IAnnotationRepository() = default;

    virtual AnnotationSet load(const std::string& slideId) = 0;
    virtual void save(const std::string& slideId, const AnnotationSet& annotations) = 0;
    virtual void exportTo(const std::string& slideId, const std::string& path,
                          ExportFormat format) = 0;
    virtual AnnotationSet importFrom(const std::string& path,
                                     ImportFormat format) = 0;
};
```

### 6.4 IPlugin (Plugin Interface)

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual PluginInfo info() const = 0;  // name, version, description, author
    virtual bool initialize(PluginContext& context) = 0;
    virtual void shutdown() = 0;
};
```

### 6.5 ViewportController API

```cpp
class ViewportController {
public:
    // Coordinate transforms
    QPointF screenToSlide(QPointF screenPos) const;
    QPointF slideToScreen(QPointF slidePos) const;
    QRectF visibleSlideRect() const;

    // Navigation
    void setCenter(QPointF slidePos);
    void setZoomLevel(double magnification);
    void zoomAtPoint(QPointF screenPos, double factor);
    void fitToScreen();
    void panBy(QPointF screenDelta);

    // Tile calculation
    int currentPyramidLevel() const;
    std::vector<TileCoord> visibleTileCoords() const;
    std::vector<TileCoord> prefetchTileCoords() const;

    // State
    double currentMagnification() const;
    double currentMicronsPerPixel() const;

signals:
    void viewChanged();  // emitted on any pan/zoom change
};
```

### 6.6 AnnotationModel API

```cpp
class AnnotationModel : public QObject {
public:
    // CRUD
    void addAnnotation(std::unique_ptr<Annotation> annotation);
    void removeAnnotation(const std::string& id);
    void updateAnnotation(const std::string& id, AnnotationProperties props);

    // Query
    Annotation* findById(const std::string& id) const;
    std::vector<Annotation*> findInRect(QRectF slideRect) const;
    std::vector<Annotation*> findByLayer(const std::string& layerId) const;
    Annotation* hitTest(QPointF slidePos, double tolerance) const;

    // Layers
    void addLayer(const std::string& name);
    void removeLayer(const std::string& id);
    void setLayerVisible(const std::string& id, bool visible);
    void setLayerLocked(const std::string& id, bool locked);
    std::vector<AnnotationLayer> layers() const;

    // Serialization
    AnnotationSet toAnnotationSet() const;
    void fromAnnotationSet(const AnnotationSet& set);

signals:
    void annotationAdded(const std::string& id);
    void annotationRemoved(const std::string& id);
    void annotationChanged(const std::string& id);
    void layerChanged(const std::string& layerId);
    void modelModified();  // any change, used by auto-save
};
```

---

## 7. Integration with SlideIO

### 7.1 Wrapping Strategy

SlideIO is accessed exclusively through the `SlideIOAdapter` class, which implements the `ISlideSource` interface. No other component in the application imports SlideIO headers or calls SlideIO functions directly. This isolation provides:

- **Testability:** Unit tests can mock `ISlideSource` without needing real slide files.
- **Upgradability:** SlideIO version upgrades affect only the adapter.
- **Format neutrality:** The application is unaware of SVS, NDPI, MRXS, or any other format.

### 7.2 SlideIOAdapter Implementation

```cpp
class SlideIOAdapter : public ISlideSource {
public:
    explicit SlideIOAdapter(const std::string& filePath);

    SlideMetadata metadata() const override;
    int pyramidLevelCount() const override;
    QSize levelDimensions(int level) const override;
    double resolution() const override;
    int channelCount() const override;

    TileData readTile(int level, int tileX, int tileY,
                      int tileWidth, int tileHeight) const override;

    QImage thumbnail(int maxWidth, int maxHeight) const override;
    void close() override;

private:
    std::shared_ptr<slideio::Slide> m_slide;
    std::shared_ptr<slideio::Scene> m_scene;
    mutable std::mutex m_readMutex;  // SlideIO may not be thread-safe per-scene
};
```

### 7.3 Thread Safety

SlideIO's thread safety model varies by format driver. The adapter guards all `readTile` calls with a per-scene mutex. To achieve parallelism for tile decoding, the application can open multiple `SlideIOAdapter` instances for the same file (one per decode thread) or use a pool of adapters. The chosen strategy:

- **Adapter pool:** A `SlideIOAdapterPool` maintains N adapter instances per slide (where N = thread pool size). Each worker thread checks out an adapter, performs the read, and returns it. This avoids mutex contention while respecting SlideIO's per-instance thread model.

```cpp
class SlideIOAdapterPool {
public:
    explicit SlideIOAdapterPool(const std::string& filePath, int poolSize);
    std::shared_ptr<SlideIOAdapter> acquire();  // blocks if all in use
    void release(std::shared_ptr<SlideIOAdapter> adapter);
private:
    std::queue<std::shared_ptr<SlideIOAdapter>> m_available;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};
```

### 7.4 Error Handling

- SlideIO exceptions are caught at the adapter boundary and translated to application-specific error types.
- Corrupt tiles return an error `TileData` with an error flag; the renderer displays a hatched pattern for that tile.
- If a slide file cannot be opened, the adapter throws `SlideOpenException` with a user-friendly message.

### 7.5 Metadata Extraction

On slide open, `MetadataReader` extracts all available metadata from SlideIO:

```cpp
struct SlideMetadata {
    std::string filePath;
    std::string scannerVendor;
    std::string scannerModel;
    std::string scanDate;
    QSize fullDimensions;         // pixels at level 0
    double micronsPerPixel;       // at level 0
    double objectiveMagnification;
    int pyramidLevels;
    int channelCount;
    std::vector<std::string> channelNames;
    int zStackCount;
    std::string compressionMethod;
    std::map<std::string, std::string> rawMetadata;  // all key-value pairs
};
```

---

## 8. Image Tiling and Caching

### 8.1 Tile Pyramid Structure

Whole-slide images are stored as multi-resolution pyramids. The application models this as:

```cpp
struct TilePyramid {
    int levelCount;               // typically 3-8 levels
    int tileWidth = 256;          // standard tile size
    int tileHeight = 256;
    struct Level {
        int index;                // 0 = highest resolution
        QSize dimensions;         // pixel dimensions at this level
        double downsampleFactor;  // 1.0 for level 0, 2.0 for level 1, etc.
        int tilesX;               // number of tile columns
        int tilesY;               // number of tile rows
    };
    std::vector<Level> levels;
};
```

The ViewportController selects the pyramid level whose resolution best matches the current screen-space pixel density. Specifically, it picks the level where `downsampleFactor <= screenPixelSize / slidePixelSize`, choosing the highest resolution that does not oversample.

### 8.2 Tile Size

Fixed tile size of **256x256 pixels**. This balances:
- **GPU efficiency:** 256x256 is a natural texture size; OpenGL uploads are efficient.
- **Decode granularity:** Small enough that individual tile decodes are fast (1-5ms), allowing responsive progressive loading.
- **Cache granularity:** Small enough that LRU eviction has fine control over memory.
- **Prefetch overhead:** Not so small that the number of visible tiles becomes excessive (a 1920x1080 viewport at full resolution shows ~32 tiles).

### 8.3 LRU Tile Cache

The tile cache is the performance-critical data structure.

**Structure:**
- Hash map from `TileKey` to `TileEntry` for O(1) lookup.
- Doubly-linked list for LRU ordering.
- Each `TileEntry` holds: decoded RGBA pixel buffer, GPU texture handle, byte size, access timestamp.

**Memory accounting:**
- Each 256x256 RGBA tile = 256 KB (CPU buffer) + 256 KB (GPU texture) = 512 KB total.
- With a 2 GB tile cache budget: ~4,096 tiles in cache.
- This covers approximately 16 full-viewport loads at 1080p, sufficient for smooth back-and-forth navigation.

**Eviction policy:**
- LRU with priority boost for tiles at the current zoom level and within one tile ring of the viewport.
- When memory usage exceeds 90% of budget, evict tiles from the least-recently-used end.
- Tiles from closed slides are evicted immediately.

```cpp
class LruTileCache : public ITileCache {
public:
    explicit LruTileCache(size_t memoryBudget);

    std::optional<TileData> get(const TileKey& key) override;
    void put(const TileKey& key, TileData data) override;
    void evict(const std::string& slideId) override;
    size_t memoryUsage() const override;
    void setMemoryBudget(size_t bytes) override;

private:
    struct Entry {
        TileKey key;
        TileData data;
        size_t byteSize;
    };
    std::unordered_map<TileKey, std::list<Entry>::iterator> m_map;
    std::list<Entry> m_lruList;  // front = most recent, back = least recent
    size_t m_memoryBudget;
    size_t m_currentUsage = 0;
    mutable std::shared_mutex m_mutex;  // readers-writer lock
};
```

### 8.4 Prefetching Strategy

The Prefetcher runs on a dedicated thread and proactively loads tiles that the user is likely to need next.

**Prefetch rings:**
1. **Ring 0 (immediate):** All tiles visible in the current viewport. Highest priority.
2. **Ring 1 (spatial):** Tiles one tile-width beyond the viewport in all directions. Medium priority.
3. **Ring 2 (zoom):** All tiles for the current viewport area at one zoom level above and below. Lower priority.
4. **Ring 3 (directional):** If pan velocity is nonzero, prefetch 2-3 tiles ahead in the pan direction. Medium priority.

**Priority queue:** Tile decode requests are placed in a priority queue. The thread pool picks the highest-priority request first. If the viewport changes (user pans/zooms), outdated prefetch requests are cancelled.

```cpp
class Prefetcher {
public:
    void onViewportChanged(const ViewportState& state);
    void stop();

private:
    void prefetchLoop();
    PriorityQueue<TileRequest> m_requestQueue;
    std::atomic<bool> m_running{true};
    std::thread m_thread;
    ViewportState m_lastState;
    QPointF m_panVelocity;
};
```

### 8.5 Memory Budget Configuration

| Scenario | Cache Budget | Rationale |
|----------|-------------|-----------|
| Single slide, standard workstation | 2 GB | ~4096 tiles, covers extensive navigation |
| Multi-slide comparison (2-4 slides) | 3 GB | Split budget across slides proportionally |
| Low-memory machine (<8 GB RAM) | 1 GB | Reduced cache, more tile reloading |
| High-end workstation (32+ GB) | 4-8 GB | Extended cache for research workflows |

The memory budget is user-configurable in preferences with sensible defaults based on available system RAM (detected at startup): 25% of available RAM, clamped to [512 MB, 8 GB].

---

## 9. Remote Image Access

### 9.1 Architecture

Remote slide access uses a REST-based tile server protocol. The `RemoteSlideClient` implements `ISlideSource` identically to `SlideIOAdapter`, making remote and local slides interchangeable from the application's perspective.

```
Application <--> RemoteSlideClient <--> HTTP/HTTPS <--> Tile Server
                        |
                        v
                  DiskTileCache (local)
```

### 9.2 Tile Server Protocol

The application expects a tile server that exposes the following REST endpoints:

```
GET /slides/{slide_id}/info
    Returns: JSON with slide metadata (dimensions, levels, resolution, channels)

GET /slides/{slide_id}/tile/{level}/{x}/{y}?format=jpeg&quality=85
    Returns: JPEG or PNG tile image data

GET /slides/{slide_id}/thumbnail?maxWidth=300&maxHeight=300
    Returns: JPEG thumbnail image

GET /slides/{slide_id}/label
    Returns: Label/macro image (if available)
```

This is a standard tile-server API compatible with common pathology image servers. The application does not implement the server; it is a client only.

### 9.3 RemoteSlideClient

```cpp
class RemoteSlideClient : public ISlideSource {
public:
    explicit RemoteSlideClient(const QUrl& baseUrl, const std::string& slideId);

    // ISlideSource implementation
    TileData readTile(int level, int tileX, int tileY,
                      int tileWidth, int tileHeight) const override;
    // ... other methods

private:
    QUrl m_baseUrl;
    std::string m_slideId;
    QNetworkAccessManager* m_network;  // owned by network thread
    DiskTileCache* m_diskCache;
    SlideMetadata m_cachedMetadata;
};
```

### 9.4 Local Disk Cache for Remote Tiles

Remote tiles are cached on disk to avoid re-downloading across sessions.

**Structure:**
```
~/.slideio-viewer/cache/
    {server_hash}/
        {slide_id}/
            info.json           # cached metadata
            tiles/
                {level}_{x}_{y}.jpg
```

**Eviction:** Total disk cache size is limited (default 10 GB, configurable). LRU eviction based on file access time. Cache is cleaned on application startup if it exceeds the limit.

**Cache-first strategy:**
1. Check in-memory LRU cache.
2. Check disk cache.
3. Fetch from server.
4. Store in both disk cache and memory cache.

### 9.5 Connection Handling

- **Timeout:** 10 seconds for metadata requests, 15 seconds per tile.
- **Retry:** Up to 3 retries with exponential backoff (1s, 2s, 4s) for failed tile requests.
- **Concurrent requests:** Up to 6 parallel HTTP requests per server (browser convention).
- **Connection loss:** Status bar shows "Connection lost. Showing cached data." Cached tiles remain available. Auto-retry every 5 seconds.
- **TLS:** HTTPS enforced for remote connections. Certificate validation follows system trust store.

---

## 10. Annotation Storage

### 10.1 Storage Format

Annotations are stored in a JSON format based on GeoJSON conventions, extended with pathology-specific properties. Each slide's annotations are stored in a separate file.

**File naming convention:**
```
{slide_filename}.annotations.json
```
Example: `case001_HE.svs.annotations.json`

### 10.2 Annotation JSON Schema

```json
{
  "version": "1.0",
  "slideId": "case001_HE.svs",
  "slideDimensions": { "width": 100000, "height": 80000 },
  "slideResolution": 0.25,
  "coordinateSystem": "pixels_level0",
  "layers": [
    {
      "id": "layer-uuid-1",
      "name": "Diagnostic",
      "visible": true,
      "locked": false,
      "opacity": 1.0,
      "order": 0
    }
  ],
  "annotations": [
    {
      "id": "ann-uuid-1",
      "type": "polygon",
      "layerId": "layer-uuid-1",
      "geometry": {
        "type": "Polygon",
        "coordinates": [[
          [45230.5, 12890.0],
          [45500.0, 12890.0],
          [45500.0, 13200.0],
          [45230.5, 13200.0],
          [45230.5, 12890.0]
        ]]
      },
      "properties": {
        "label": "Invasive carcinoma",
        "classification": "Tumor",
        "color": "#E67E22",
        "lineWidth": 2,
        "fillOpacity": 0.3,
        "confidence": "certain",
        "notes": ""
      },
      "metadata": {
        "author": "dr.smith",
        "createdAt": "2026-03-13T14:30:00Z",
        "modifiedAt": "2026-03-13T14:35:00Z"
      }
    }
  ]
}
```

### 10.3 Geometry Types Mapping

| Annotation Type | GeoJSON Geometry Type | Notes |
|----------------|----------------------|-------|
| Rectangle | Polygon (4 vertices + close) | Axis-aligned bounding box |
| Ellipse | Custom: `{ "type": "Ellipse", "center": [x,y], "radiusX": r1, "radiusY": r2, "rotation": deg }` | Extended type, not standard GeoJSON |
| Freehand Polygon | Polygon | Standard GeoJSON polygon |
| Freehand Line | LineString | Standard GeoJSON |
| Point Marker | Point | Standard GeoJSON |
| Arrow | Custom: `{ "type": "Arrow", "tail": [x,y], "head": [x,y] }` | Extended type |
| Ruler | LineString (2 points) + measurement property | Length stored in properties |
| Area Measurement | Polygon + measurement property | Area stored in properties |
| Angle Measurement | Custom: `{ "type": "Angle", "vertex": [x,y], "ray1End": [x,y], "ray2End": [x,y] }` | Extended type |
| Text Label | Point + text property | Anchor position is a point |

### 10.4 Coordinate System

All geometry coordinates are in **pixel units at pyramid level 0** (the highest resolution). This is the natural coordinate system for pathology annotations because:

- It is independent of the current zoom level.
- It maps directly to physical dimensions via the slide's microns-per-pixel metadata.
- It is compatible with external tools that use the same convention.

Conversion to physical units: `physical_um = pixel_coordinate * micronsPerPixel`.

### 10.5 Versioning

The `"version"` field in the annotation file enables forward compatibility. When the schema changes:

1. Increment the version number.
2. New application versions can read all previous versions (backward compatibility).
3. Old application versions encountering a newer schema version display a warning and attempt best-effort loading.

**Migration:** On load, if the file version is older than the current version, the annotations are automatically migrated to the latest schema and re-saved.

### 10.6 Import/Export

| Format | Import | Export | Notes |
|--------|--------|--------|-------|
| Native JSON | Yes | Yes | Full fidelity |
| GeoJSON (QuPath) | Yes | Yes | Standard geometry, some property loss |
| ASAP XML | Yes | No (v1) | Common legacy format |
| CSV (measurements) | No | Yes | Flat table: id, type, label, measurement, units |

Import detects format automatically by file extension and content inspection.

---

## 11. Plugin Architecture

### 11.1 Extension Points

Plugins can extend the application at the following hook points:

| Extension Point | Description | Example |
|----------------|-------------|---------|
| `analysis.run` | Run analysis on the current slide/viewport | AI tumor detection overlay |
| `annotation.type` | Register a custom annotation type | Cell counter annotation |
| `annotation.classifier` | Auto-classify annotations | ML-based tissue classifier |
| `export.format` | Register a custom export format | DICOM SR annotation export |
| `import.format` | Register a custom import format | Proprietary annotation import |
| `toolbar.action` | Add a button to the toolbar | Custom analysis tool |
| `menu.action` | Add a menu item | Institutional workflow integration |
| `panel.custom` | Add a custom side panel | Analysis results panel |
| `slide.source` | Register a custom slide source | Cloud storage integration |
| `viewport.overlay` | Draw custom overlays on the viewport | Heatmap overlay |

### 11.2 Plugin API (PluginContext)

The `PluginContext` is passed to plugins at initialization and provides controlled access to the application.

```cpp
class PluginContext {
public:
    // Slide access (read-only)
    const ISlideSource* currentSlide() const;
    SlideMetadata slideMetadata() const;
    TileData readTile(int level, int x, int y, int w, int h) const;

    // Viewport (read-only position, can request navigation)
    QRectF visibleSlideRect() const;
    double currentMagnification() const;
    void requestNavigateTo(QPointF slidePos, double magnification);

    // Annotations (full access on plugin's own layer)
    AnnotationModel* annotationModel();
    std::string createPluginLayer(const std::string& name);

    // UI extension
    void addToolbarAction(const std::string& group, QAction* action);
    void addMenuItem(const std::string& menu, QAction* action);
    QWidget* addDockPanel(const std::string& title, QWidget* widget);

    // Overlay drawing
    void registerOverlayPainter(std::function<void(QPainter&, const ViewportState&)>);

    // Progress reporting
    void reportProgress(double fraction, const std::string& message);

    // Logging
    void log(LogLevel level, const std::string& message);
};
```

### 11.3 Plugin Discovery and Loading

Plugins are shared libraries (.dll/.so/.dylib) placed in a `plugins/` directory relative to the application binary, or in a user-configured directory.

**Loading sequence:**
1. Application scans plugin directories for shared libraries.
2. Each library exports a C factory function: `IPlugin* createPlugin()`.
3. Application calls `createPlugin()`, then `plugin->info()` to display in the plugin manager.
4. Enabled plugins have `plugin->initialize(context)` called during startup.
5. On shutdown, `plugin->shutdown()` is called in reverse order.

**Stability boundary:** Plugins run in the same process for performance (no IPC overhead). A misbehaving plugin can crash the application. Mitigation: plugin operations that access SlideIO or annotations are wrapped in try-catch at the plugin boundary. A plugin manager UI shows loaded plugins with enable/disable toggles.

### 11.4 Plugin Versioning

The plugin API declares a version number. Plugins are compiled against a specific API version. At load time, the application checks compatibility:

```cpp
constexpr int PLUGIN_API_VERSION_MAJOR = 1;
constexpr int PLUGIN_API_VERSION_MINOR = 0;
```

- Same major version: compatible (minor additions are backward-compatible).
- Different major version: incompatible, plugin is not loaded, warning displayed.

---

## 12. Technology Choices

### 12.1 Core Technologies

| Technology | Choice | Rationale |
|-----------|--------|-----------|
| **Language** | C++17 (with select C++20 features) | Performance-critical application; C++17 is widely supported; C++20 used for concepts, `std::jthread`, `std::format` where available |
| **UI Framework** | Qt 6 (Widgets + QOpenGLWidget) | Mature cross-platform UI; native look-and-feel; QOpenGLWidget for GPU tile rendering; docking framework for detachable panels |
| **Slide Library** | SlideIO | Multi-format WSI access; Python and C++ bindings; active development |
| **Rendering** | OpenGL 3.3+ via QOpenGLWidget | Hardware-accelerated tile rendering; widely supported; sufficient for 2D tile compositing |
| **Build System** | CMake 3.21+ | Industry standard for C++; Qt 6 integration; supports all target platforms |
| **Package Manager** | Conan 2 | Cross-platform dependency management; large package ecosystem; integrates with CMake via CMakeDeps/CMakeToolchain generators |

### 12.2 Key Libraries

| Library | Purpose |
|---------|---------|
| **Qt 6 Core** | Event loop, threading (QThread, QThreadPool), file I/O, JSON |
| **Qt 6 Widgets** | Main window, panels, toolbars, menus, dialogs, dock widgets |
| **Qt 6 OpenGL** | QOpenGLWidget, shader programs, texture management |
| **Qt 6 Network** | QNetworkAccessManager for HTTP/HTTPS remote tile access |
| **nlohmann/json** | Annotation JSON serialization (more ergonomic than Qt's QJsonDocument for complex schemas) |
| **spdlog** | Structured logging with file rotation |
| **Catch2** | Unit testing framework |
| **Google Benchmark** | Performance benchmarking for cache and rendering paths |

### 12.3 Why Qt Widgets over QML

Qt Widgets is chosen over QML for the following reasons:

1. **Mature docking framework:** QDockWidget provides the detachable panel functionality required by the UX design. QML has no equivalent.
2. **OpenGL integration:** QOpenGLWidget provides direct OpenGL context management needed for custom tile rendering. QML's Scene Graph adds abstraction overhead.
3. **Desktop-first:** The application is desktop-only. QML's strengths (touch, animations, mobile) are less relevant.
4. **Debugging:** Widget-based code is easier to debug with standard C++ tools.

### 12.4 Why OpenGL 3.3+ over Vulkan

1. **Sufficient for the task:** 2D tile rendering with texture atlases does not need Vulkan's multi-command-buffer parallelism.
2. **Simpler code:** OpenGL requires significantly less boilerplate for the operations we need (texture upload, textured quad rendering, alpha blending).
3. **Wider support:** OpenGL 3.3 is supported on virtually all hardware from 2010 onwards, including Intel integrated GPUs in clinical workstations.
4. **Qt integration:** QOpenGLWidget is well-tested and stable. Qt's RHI abstraction can be adopted later if Vulkan becomes necessary.

### 12.5 Build and CI

- **CMake** with `FetchContent` for header-only libraries, Conan for compiled dependencies.
- **CI/CD:** GitHub Actions with matrix builds for Windows (MSVC 2022), macOS (AppleClang), and Ubuntu 22.04 (GCC 12).
- **Packaging:** CPack for installers (NSIS on Windows, DMG on macOS, AppImage on Linux).

---

## 13. Cross-Cutting Concerns

### 13.1 Error Handling Strategy

- Domain layer uses `std::expected` (C++23) or a custom `Result<T, Error>` type for recoverable errors.
- Infrastructure layer translates external exceptions (SlideIO, file I/O, network) to application error types at the boundary.
- Presentation layer displays errors via non-modal notification banners (not modal dialogs) for non-critical errors, and modal dialogs only for fatal/blocking errors.

### 13.2 Configuration

- Application settings stored in `QSettings` (platform-native: registry on Windows, plist on macOS, ini on Linux).
- Settings include: memory budget, tile cache size, UI layout, theme, shortcuts, default annotation colors, plugin paths.
- Settings are accessible via a `ConfigService` singleton injected into components.

### 13.3 Logging

- `spdlog` with two sinks: rotating file (debug level) and console/stdout (info level in debug builds).
- Log file location: `~/.slideio-viewer/logs/viewer.log`.
- Structured log format: `[timestamp] [level] [component] message`.

### 13.4 Session Recovery

On startup, the application checks for a `.session` file:
- If present and the application did not exit cleanly, restore: open slides, viewport positions, unsaved annotations from auto-save.
- The session file is updated on every slide open/close and deleted on clean exit.

---

*This document serves as input for Phase 4 (Implementation Strategy) and ultimately feeds into Document 3 (Software Architecture and Design).*
