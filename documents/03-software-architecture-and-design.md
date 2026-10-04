# Document 3: Software Architecture and Design

**Project:** SlideIO Viewer -- Cross-Platform Pathology Slide Viewer
**Version:** 1.0
**Date:** 2026-03-13
**Based on:** Phase 1 Requirements Discovery, Phase 2 UX Concept, Phase 3 System Architecture, Phase 4 Implementation Strategy, Phase 5 Critical Review

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Architectural Style](#2-architectural-style)
3. [Major Components](#3-major-components)
4. [Data Flow](#4-data-flow)
5. [Module Descriptions](#5-module-descriptions)
6. [Integration with SlideIO](#6-integration-with-slideio)
7. [Image Tiling and Caching Strategy](#7-image-tiling-and-caching-strategy)
8. [Remote Image Loading](#8-remote-image-loading)
9. [Annotation Storage](#9-annotation-storage)
10. [Key Class Structures](#10-key-class-structures)
11. [Threading Model](#11-threading-model)
12. [Plugin Architecture](#12-plugin-architecture)
13. [Security and Identity](#13-security-and-identity)
14. [Technology Choices](#14-technology-choices)
15. [Cross-Cutting Concerns](#15-cross-cutting-concerns)

---

## 1. System Overview

SlideIO Viewer is a cross-platform desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It serves clinical pathologists performing primary diagnosis, consultants reviewing shared cases, researchers conducting quantitative analysis, and trainees learning histopathology.

The application manages gigapixel-scale images (50,000 x 50,000 to 200,000 x 100,000 pixels, 500 MB to 10+ GB per file) with sub-second responsiveness and smooth 60fps pan/zoom. It achieves this through GPU-accelerated tile rendering, multi-threaded tile decoding, LRU caching with memory budgets, and progressive tile refinement.

### 1.1 Key Architectural Drivers

| Driver | Requirement | Impact on Architecture |
|--------|-------------|----------------------|
| **Performance** | 60fps pan/zoom on gigapixel images | GPU rendering, tile prefetching, async decode, LRU cache |
| **Format neutrality** | Support 10+ slide formats transparently | SlideIO adapter pattern, `ISlideSource` abstraction |
| **Annotation independence** | Annotations stored separately, versioned, exportable | JSON-based storage, content-derived slide IDs, file locking |
| **Cross-platform** | Windows 10+, macOS 12+, Ubuntu 22.04+ | Qt 6 Widgets, C++17, OpenGL 3.3 with renderer abstraction |
| **Memory budget** | 4 GB typical usage, 8 GB multi-slide | LRU cache with configurable budget, separate GPU memory tracking |
| **Extensibility** | Plugin system for AI analysis, custom tools | Shared library plugins, stable versioned API, safe mode |
| **Clinical reliability** | No data loss, 8+ hour stable sessions | Auto-save, session recovery, crash-proof annotation persistence |
| **Privacy compliance** | HIPAA/GDPR-ready for institutional deployment | Default-hidden labels, export redaction, structured audit logging |

### 1.2 Scope Boundaries

The application is a **desktop viewer and annotation tool**. It is not:
- A slide scanner controller.
- A laboratory information system (LIS).
- A tile server (it is a client only for remote access).
- A real-time collaboration platform (file-based sharing in v1).

---

## 2. Architectural Style

The application follows a **layered architecture** with strict downward-only dependency direction. Cross-cutting concerns (logging, configuration, error handling) are accessible to all layers via dependency injection.

```
+------------------------------------------------------------------+
|                    Presentation Layer (Qt UI)                     |
|  MainWindow, ViewportWidget, Panels, Toolbars, Dialogs           |
+------------------------------------------------------------------+
         |  depends on
         v
+------------------------------------------------------------------+
|                    Application Layer (Use Cases)                  |
|  SlideViewerService, AnnotationService, CaseService,             |
|  BookmarkService, SnapshotService, PluginService                 |
+------------------------------------------------------------------+
         |  depends on
         v
+------------------------------------------------------------------+
|                    Domain Layer (Core Logic)                      |
|  Annotation, Case, Layer, Bookmark, Measurement,                 |
|  TilePyramid, Viewport, CoordinateSystem, AnnotationGeometry     |
+------------------------------------------------------------------+
         ^  implements interfaces defined here
         |
+------------------------------------------------------------------+
|                    Infrastructure Layer                           |
|  SlideIOAdapter, TileCache, TileLoader, RemoteClient,            |
|  AnnotationStore, ConfigStore, PluginLoader, UserIdentity         |
+------------------------------------------------------------------+
```

### 2.1 Layer Responsibilities

| Layer | Responsibility | Dependencies | Qt Dependency |
|-------|---------------|--------------|---------------|
| **Presentation** | Qt widgets, event handling, OpenGL rendering, user input, keyboard shortcuts | Application | Full (Widgets, OpenGL) |
| **Application** | Use-case orchestration, coordinate domain objects, enforce workflows, undo/redo | Domain | Qt Core only (signals) |
| **Domain** | Core entities, annotation geometry, coordinate math, business rules, interface definitions | None | None (pure C++17) |
| **Infrastructure** | SlideIO access, file I/O, network, caching, plugin loading, persistence | Domain interfaces | Qt Core, Qt Network |

The Domain layer defines abstract interfaces (`ISlideSource`, `ITileCache`, `IAnnotationRepository`, `ICaseRepository`, `IUserIdentity`) that the Infrastructure layer implements. The Application layer wires them together via constructor injection. This inversion of control makes every layer independently testable with mocks.

### 2.2 Build-Time Enforcement

Each layer compiles as a separate CMake static library target. The dependency rules are enforced at the build system level:

| Target | Type | Links Against |
|--------|------|--------------|
| `slideio-viewer-core` | STATIC | None (pure C++) |
| `slideio-viewer-app` | STATIC | core, Qt6::Core |
| `slideio-viewer-infra` | STATIC | core, app, Qt6::Core, Qt6::Network, SlideIO, nlohmann_json, spdlog |
| `slideio-viewer-ui` | STATIC | core, app, infra, Qt6::Widgets, Qt6::OpenGLWidgets |
| `slideio-viewer` | EXECUTABLE | ui (transitively links all) |

The `core` target cannot accidentally include Qt headers because it does not link against any Qt target. A `#include <QObject>` in a core file produces a build error.

---

## 3. Major Components

### 3.1 Component Diagram

```
+-------------------+     +-------------------+     +-------------------+
|   Viewer Engine   |     | Annotation Engine |     |   Case Manager    |
|   - Viewport      |     | - Drawing tools   |     |   - Case model    |
|   - Tile renderer |<--->| - Geometry engine |<--->|   - Slide list    |
|   - Zoom/pan ctrl |     | - Layer manager   |     |   - Metadata      |
|   - Split view    |     | - Undo/redo stack |     |   - Bookmarks     |
|   - Nav history   |     | - Search/filter   |     |   - User identity |
+--------+----------+     +---------+---------+     +-------------------+
         |                          |
         v                          v
+-------------------+     +-------------------+     +-------------------+
|   Cache Manager   |     | Annotation Store  |     |  Plugin System    |
|   - LRU tile cache|     | - JSON serializer |     |  - Plugin loader  |
|   - Memory budget |     | - Import/export   |     |  - Extension API  |
|   - GPU mem track |     | - Version control |     |  - Safe mode      |
|   - Prefetcher    |     | - File locking    |     |  - Code signing   |
+--------+----------+     | - Incremental save|     +-------------------+
         |                 +-------------------+
         v
+-------------------+     +-------------------+
|   Slide Manager   |     | Remote Access     |
|   - SlideIO wrap  |     | - HTTP client     |
|   - Adapter pool  |     | - Auth (OAuth/TLS)|
|   - Metadata read |     | - Disk tile cache |
|   - Variable tiles|     | - Rate limiting   |
+-------------------+     +-------------------+
```

### 3.2 Component Summary

| Component | Responsibility | Key Design Decision |
|-----------|---------------|-------------------|
| **Viewer Engine** | Tile rendering, viewport management, navigation, split view | Custom QOpenGLWidget; not QGraphicsView |
| **Annotation Engine** | Drawing tools, geometry operations, layers, undo/redo | Command pattern for undo; `std::variant` for geometry |
| **Case Manager** | Slide grouping, metadata, bookmarks, case persistence | `case.json` manifest format (addressing review finding 1.2) |
| **Cache Manager** | LRU tile cache, memory budget, GPU memory tracking, prefetching | Separate CPU and GPU memory budgets (addressing review finding 2.5) |
| **Annotation Store** | JSON persistence, import/export, auto-save, file locking | Content-derived slide IDs (addressing review finding 1.5); file locking (review 1.3) |
| **Slide Manager** | SlideIO wrapper, tile decoding, metadata extraction | Adaptive adapter pool (addressing review finding 2.1); variable tile sizes (review 2.2) |
| **Remote Access** | HTTP tile fetching, disk cache, authentication, rate limiting | OAuth 2.0 / client cert auth (addressing review finding 5.4); rate limiting (review 4.4) |
| **Plugin System** | Plugin loading, extension API, safe mode | Code signing verification, auto-disable on crash (addressing review finding 4.3) |

---

## 4. Data Flow

### 4.1 Tile Loading Pipeline

```
User pans/zooms viewport
        |
        v
ViewportController calculates visible tile coordinates
  (slide_id, pyramid_level, tile_col, tile_row)
  using the slide's native tile size (not a fixed 256x256)
        |
        v
LruTileCache lookup ----[HIT]----> GPU texture cache lookup
        |                             |
     [MISS]                    [HIT]  |  [MISS]
        |                      |      v       |
        v                      | Upload to    |
TileLoadScheduler enqueues     | GPU (PBO     |
request at Visible priority    | async)       |
        |                      v              v
        v                  Render tile    Re-upload from
SlideIOAdapterPool.acquire()              CPU buffer
  -> adapter.readTile()                       |
        |                                     v
        v                                Render tile
Decoded RGBA pixel buffer
        |
        v
LruTileCache.put() (evicts LRU if over budget)
        |
        v
Signal to UI thread: tile ready (Qt::QueuedConnection)
        |
        v
ViewportWidget: upload to GPU via PBO (max 4-8 per frame)
  crossfade from lower-resolution placeholder
        |
        v
Display sharp tile on screen
```

### 4.2 Annotation Creation Flow

```
User presses tool hotkey (e.g., R for Rectangle)
        |
        v
DrawingToolController enters Rectangle creation mode
  Cursor changes to crosshair
  Status bar shows: "Rectangle | Click-drag to draw"
        |
        v
User clicks and drags on viewport
  ViewportWidget translates screen coords to slide coords
  via ViewportController.screenToSlide()
        |
        v
DrawingToolController renders real-time preview overlay
        |
        v
User releases mouse -> creation complete
        |
        v
DrawingToolController creates Annotation domain object:
  - Geometry in level-0 pixel coordinates
  - Default properties (color from active classification)
  - Active layer ID
  - Author from UserIdentity service
  - Timestamp
        |
        v
UndoRedoStack records CreateAnnotationCommand
        |
        v
AnnotationModel.addAnnotation()
        |
        +----> Signal: annotationAdded
                  |
                  +--> ViewportWidget repaints annotation overlay
                  +--> AnnotationListPanel updates list
                  +--> AutoSaver schedules debounced save (2s)
```

### 4.3 Slide Open Flow

```
User opens slide (File > Open, drag-drop, or case panel click)
        |
        v
SlideViewerService.openSlide(path_or_url)
        |
        v
Determine source type: local path vs. remote URL
        |
        +--[local]---> SlideIOAdapterPool.create(path, poolSize)
        |                     |
        +--[remote]--> RemoteSlideClient.connect(url, credentials)
                              |
                              v
                       ISlideSource instance created
                              |
                              v
MetadataReader extracts: dimensions, resolution (um/px),
  native tile size, pyramid levels, channel count, scanner info
        |
        v
Generate content-derived slide ID:
  hash(file_size + dimensions + scan_date + scanner_id)
        |
        v
Validate calibration metadata:
  if micronsPerPixel missing -> set calibration warning flag
        |
        v
TilePyramid model constructed using native tile size
  (not hardcoded 256x256)
        |
        v
ViewportController initializes: fit-to-screen with 5% margin
        |
        v
TileRenderer requests tiles for initial viewport
  -> Tile loading pipeline executes
        |
        v
AnnotationStore.load(slideId) with file lock:
  - Verify annotation file's embedded slideId matches
  - Deserialize JSON
  - Check schema version, migrate if needed
        |
        v
Slide displayed with annotations overlaid
  Target: <2s local, <5s remote to first view
```

### 4.4 Case Persistence Format

Addressing the critical review finding that no case file format was defined:

```json
{
  "version": "1.0",
  "caseId": "uuid-v4",
  "metadata": {
    "accessionNumber": "S24-12345",
    "patientId": "anon-001",
    "clinicalHistory": "Colon biopsy, rule out dysplasia",
    "specimenType": "Colon, right hemicolectomy",
    "dateOfProcedure": "2026-03-10"
  },
  "slides": [
    {
      "slideId": "content-hash-abc123",
      "relativePath": "slides/case001_HE.svs",
      "absolutePath": "/data/pathology/slides/case001_HE.svs",
      "stainType": "H&E",
      "blockId": "A1",
      "sectionNumber": 1,
      "qualityFlags": []
    }
  ],
  "bookmarks": [
    {
      "name": "Tumor focus",
      "slideId": "content-hash-abc123",
      "center": { "x": 45230, "y": 12890 },
      "magnification": 20.0,
      "activeLayers": ["layer-uuid-1"]
    }
  ],
  "createdAt": "2026-03-13T10:00:00Z",
  "modifiedAt": "2026-03-13T14:30:00Z"
}
```

The case file is stored as `{case_name}.case.json` alongside the slide files or in a user-configured workspace directory. Slide paths are stored both as relative (portable) and absolute (fast lookup). On load, relative paths are resolved first; if not found, absolute paths are tried.

---

## 5. Module Descriptions

### 5.1 Viewer Engine Module

**Purpose:** Renders slide tiles onto the screen and handles user navigation.

**Key classes:**

| Class | Responsibility |
|-------|---------------|
| `ViewportController` | Coordinate transforms (screen <-> slide <-> tile), zoom/pan state, pyramid level selection, visible tile calculation |
| `ViewportWidget` | QOpenGLWidget subclass: OpenGL rendering, mouse/keyboard event routing, GPU texture management |
| `TileRenderer` | Renders visible tiles as textured quads via instanced rendering; handles progressive refinement crossfade |
| `SplitViewController` | Manages 1-4 viewports in a grid; handles synchronized pan/zoom across linked viewports |
| `NavigationHistory` | Stores viewport states for back/forward navigation (100 entries per session) |
| `MinimapWidget` | Floating overview widget showing slide thumbnail with viewport extent rectangle |
| `ZoomIndicatorWidget` | Floating zoom level display with slider and magnification presets |

**Progressive tile refinement algorithm:**
1. For each visible tile coordinate, check the LRU cache at the target pyramid level.
2. If cached: render at full resolution.
3. If not cached: find the nearest cached tile at a lower resolution that covers the same region. Render it scaled up with bilinear filtering (appears blurry but immediate).
4. When the target-resolution tile arrives: crossfade from the placeholder to the sharp tile over 100ms using alpha blending.
5. The user never sees blank or gray tiles.

**GPU texture upload strategy (addressing review finding 2.4):**
- Use Pixel Buffer Objects (PBOs) for asynchronous texture upload.
- Limit to 4-8 texture uploads per frame to avoid GPU bus stalls.
- For remaining tiles awaiting upload, continue displaying the lower-resolution placeholder.

### 5.2 Annotation Engine Module

**Purpose:** Manages all annotation creation, editing, storage, and display.

**Key classes:**

| Class | Responsibility |
|-------|---------------|
| `AnnotationModel` | In-memory model of all annotations on a slide; provides signals for changes; supports query by region, layer, classification, and text search |
| `AnnotationGeometry` | `std::variant` of geometry types (Polygon, Ellipse, LineString, Point, Arrow, Angle); free functions for hit test, bounding box, area, perimeter |
| `DrawingToolController` | Active tool state machine; translates mouse events into annotation creation; renders preview overlay |
| `UndoRedoStack` | Per-slide Command pattern stack (configurable 50-200 levels) |
| `LayerManager` | Annotation layer CRUD, visibility, lock, opacity, ordering |

**Annotation search and filter (addressing review finding 3.4):**
- Search box at top of annotation list panel, filtering by label text and classification.
- Sort options: by position (top-left to bottom-right), creation date, classification.
- "Jump to next/previous annotation" shortcut (Ctrl+] / Ctrl+[) for sequential review.

### 5.3 Case Manager Module

**Purpose:** Manages logical grouping of slides into cases with metadata and bookmarks.

**Key classes:**

| Class | Responsibility |
|-------|---------------|
| `CaseModel` | Case entity with slides, metadata, bookmarks |
| `CaseRepository` | Persistence of `case.json` files |
| `SlideList` | Ordered slide collection with thumbnails, stain labels, quality flags; supports virtual scrolling for large cohorts |
| `BookmarkManager` | Named and quick-bookmark (Shift+1..9) management |
| `UserIdentityService` | Lightweight user identity: username from preferences, or LDAP/SSO via plugin |

### 5.4 Cache Manager Module

**Purpose:** Manages the in-memory tile cache, GPU texture cache, and prefetching.

**Key classes:**

| Class | Responsibility |
|-------|---------------|
| `LruTileCache` | Thread-safe LRU cache: hash map + doubly-linked list. Separate CPU buffer and GPU texture tracking |
| `MemoryBudget` | Monitors system RAM and GPU memory; sets and enforces cache limits; auto-adjusts for multi-slide and 4K displays |
| `Prefetcher` | Background thread: spatial ring-1 prefetch, zoom-level prefetch, directional prediction based on pan velocity |
| `GpuTextureManager` | Manages OpenGL texture lifecycle; releases GPU textures for off-screen tiles when GPU memory is constrained; re-uploads on demand |

**Memory budget strategy (addressing review findings 2.3 and 2.5):**

| Resource | Budget Calculation | Default |
|----------|-------------------|---------|
| CPU tile cache | 25% of available RAM, clamped [512 MB, 8 GB] | ~2 GB on 8 GB system |
| GPU textures | min(CPU budget, detected GPU memory * 0.5) | ~1 GB on 2 GB GPU |
| Multi-slide bonus | +50% when split view is active | +1 GB |
| 4K display bonus | +50% on displays > 2560px wide | +1 GB |

Per-viewport eviction priority: tiles from the currently active viewport are protected from eviction; tiles from background viewports are evicted first.

### 5.5 Slide Manager Module

**Purpose:** Wraps SlideIO for format-agnostic slide access.

**Key classes:**

| Class | Responsibility |
|-------|---------------|
| `SlideIOAdapter` | Implements `ISlideSource` via SlideIO; opens slides, reads metadata, decodes tiles |
| `SlideIOAdapterPool` | Manages N adapter instances per slide for parallel tile decoding without mutex contention |
| `MetadataReader` | Extracts scanner metadata, resolution, dimensions, channels, Z-stacks |
| `CalibrationManager` | Validates calibration metadata; provides manual override when metadata is missing or incorrect (addressing review finding 1.4) |

**Adaptive adapter pool (addressing review finding 2.1):**
- On slide open, start with a pool size of 1.
- Monitor decode throughput and mutex contention for the first 2 seconds of active viewing.
- If contention exceeds a threshold (>20% wait time), increase pool size by 1, up to max(4, thread_count / 2).
- For formats known to be problematic with multiple handles, cap pool size at 2. No
  format SlideIO reads is currently in that category; the rule was written for MRXS,
  which the library does not support. Keep it unimplemented until a format that needs
  it appears, and measure before applying it.

**Variable tile sizes (addressing review finding 2.2):**
- On slide open, query the native tile size from SlideIO.
- Use the native tile size for cache keys and decode requests.
- If native size > 1024: subdivide into 512x512 sub-tiles after decoding.
- If native size < 128: aggregate into 256x256 tiles.
- The cache and renderer handle variable tile sizes via the `TileData.width`/`TileData.height` fields.

### 5.6 Remote Access Module

**Purpose:** Handles accessing slides over HTTP/HTTPS from tile servers.

**Key classes:**

| Class | Responsibility |
|-------|---------------|
| `RemoteSlideClient` | Implements `ISlideSource` for HTTP tile servers; fetches metadata and tiles via REST API |
| `DiskTileCache` | Persistent on-disk cache for remote tiles (~10 GB default); LRU eviction by file access time |
| `ConnectionManager` | Connection state, retry with exponential backoff, bandwidth monitoring |
| `AuthManager` | HTTP Basic Auth, OAuth 2.0 bearer tokens, client certificate auth; integrates with OS keychain |

**REST tile server protocol:**

```
GET /slides/{slide_id}/info          -> JSON metadata
GET /slides/{slide_id}/tile/{level}/{x}/{y}?format=jpeg&quality=85  -> tile image
GET /slides/{slide_id}/thumbnail?maxWidth=300&maxHeight=300         -> thumbnail
```

**Client-side throttling (addressing review finding 4.4):**
- Maximum 60 tile requests/second (configurable).
- Maximum 6 concurrent HTTP connections per server.
- Request coalescing: when viewport changes rapidly, cancel tile requests for regions no longer visible.
- 10s timeout per metadata request, 15s per tile. Up to 3 retries with exponential backoff (1s, 2s, 4s).

---

## 6. Integration with SlideIO

### 6.1 Wrapping Strategy

SlideIO is accessed exclusively through the `SlideIOAdapter` class, which implements the domain-layer `ISlideSource` interface. No other component in the application imports SlideIO headers or calls SlideIO functions directly.

**Benefits:**
- **Testability:** Unit tests mock `ISlideSource` without needing real slide files.
- **Upgradability:** SlideIO version upgrades affect only the adapter. A compatibility test suite (one reference slide per format) validates upgrades.
- **Format neutrality:** The application is unaware of SVS, NDPI, MRXS, or any other format.

### 6.2 SlideIOAdapter Interface

```cpp
class SlideIOAdapter : public ISlideSource {
public:
    explicit SlideIOAdapter(const std::filesystem::path& filePath);

    SlideMetadata metadata() const override;
    int pyramidLevelCount() const override;
    QSize levelDimensions(int level) const override;
    double resolution() const override;           // microns per pixel at level 0
    int channelCount() const override;
    TileSize nativeTileSize() const override;     // from the file's internal tiling

    TileData readTile(int level, int tileX, int tileY,
                      int tileWidth, int tileHeight) const override;
    QImage thumbnail(int maxWidth, int maxHeight) const override;
    void close() override;

private:
    std::shared_ptr<slideio::Slide> m_slide;
    std::shared_ptr<slideio::Scene> m_scene;
    mutable std::mutex m_readMutex;
};
```

### 6.3 Adapter Pool for Parallel Decoding

```cpp
class SlideIOAdapterPool {
public:
    SlideIOAdapterPool(const std::filesystem::path& filePath, int initialPoolSize);

    // RAII loan object -- auto-returns adapter on destruction
    [[nodiscard]] AdapterLoan acquire();

    void resize(int newSize);  // adaptive pool sizing
    int currentSize() const;
    int activeCount() const;   // number currently checked out

private:
    std::filesystem::path m_filePath;
    std::queue<std::shared_ptr<SlideIOAdapter>> m_available;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_totalSize;
};

class AdapterLoan {
public:
    AdapterLoan(SlideIOAdapterPool& pool, std::shared_ptr<SlideIOAdapter> adapter);
    ~AdapterLoan();  // returns adapter to pool
    SlideIOAdapter* operator->() { return m_adapter.get(); }
    // Non-copyable, movable
private:
    SlideIOAdapterPool& m_pool;
    std::shared_ptr<SlideIOAdapter> m_adapter;
};
```

### 6.4 Error Handling at the Adapter Boundary

- SlideIO exceptions are caught and translated to `SlideOpenException` or `TileDecodeException`.
- Corrupt tiles: `readTile()` returns a `TileData` with `isError = true`. The renderer displays a hatched pattern.
- Missing metadata: `CalibrationManager` flags the slide with a warning badge. Measurements show "(uncalibrated)" until the user provides a manual calibration.

### 6.5 SlideIO Version Pinning

SlideIO is pinned to a specific version in `conanfile.py` or `FetchContent`. A compatibility test suite opens one reference slide in each supported format (SVS, AFI, NDPI, SCN, CZI, ZVI, VSI, QPTIFF, OME-TIFF, Philips TIFF, DICOM WSI, TIFF) and validates:
- Metadata extraction (dimensions, resolution, pyramid levels).
- Tile decode at level 0 (pixel data matches a stored reference checksum).
- Thumbnail generation.

This test suite runs against each SlideIO version upgrade before adoption.

---

## 7. Image Tiling and Caching Strategy

### 7.1 Tile Pyramid Model

```cpp
struct TilePyramid {
    int levelCount;             // typically 3-8
    TileSize nativeTileSize;    // from the slide file (e.g., 240x240, 256x256, 512x512)

    struct Level {
        int index;              // 0 = highest resolution
        QSize dimensions;       // pixel dimensions at this level
        double downsampleFactor;
        int tilesX;             // columns
        int tilesY;             // rows
    };
    std::vector<Level> levels;
};
```

The ViewportController selects the pyramid level whose resolution best matches the current screen pixel density: the highest-resolution level where `downsampleFactor <= screenPixelSize / slidePixelSize`, avoiding wasteful oversampling.

### 7.2 LRU Tile Cache

**Data structure:** Hash map from `TileKey` to `TileEntry` for O(1) lookup, plus a doubly-linked list for LRU ordering.

```cpp
class LruTileCache : public ITileCache {
public:
    explicit LruTileCache(size_t cpuMemoryBudget, size_t gpuMemoryBudget);

    std::optional<TileData> get(const TileKey& key) override;
    void put(const TileKey& key, TileData data) override;
    void evict(const std::string& slideId) override;

    size_t cpuMemoryUsage() const override;
    size_t gpuMemoryUsage() const override;
    void setCpuMemoryBudget(size_t bytes) override;
    void setGpuMemoryBudget(size_t bytes) override;

    // GPU texture management
    GLuint getOrUploadTexture(const TileKey& key);
    void releaseOffscreenTextures(const std::vector<TileKey>& visibleKeys);

private:
    struct Entry {
        TileKey key;
        TileData cpuData;
        GLuint gpuTexture = 0;       // 0 if not uploaded
        size_t cpuByteSize;
        size_t gpuByteSize;
        bool isVisible = false;      // protected from eviction
    };

    std::unordered_map<TileKey, std::list<Entry>::iterator> m_map;
    std::list<Entry> m_lruList;      // front = most recent
    size_t m_cpuBudget, m_gpuBudget;
    size_t m_cpuUsage = 0, m_gpuUsage = 0;
    mutable std::shared_mutex m_mutex;  // readers-writer lock
};
```

**Eviction policy:**
1. Tiles marked as visible (in current viewport) are protected.
2. Tiles from background split-view panels are evictable but at lower priority.
3. LRU eviction from the tail of the list when CPU or GPU budget is exceeded.
4. When GPU memory is limited but CPU budget remains: release GPU textures for off-screen tiles, keeping CPU buffers for fast re-upload.
5. Tiles from closed slides are evicted immediately.

### 7.3 Prefetching Strategy

The Prefetcher runs on a dedicated thread and proactively loads tiles into the cache.

| Priority | Name | Description |
|----------|------|-------------|
| 0 | Visible | Tiles in the current viewport. Loaded by TileLoadScheduler, not the prefetcher. |
| 1 | Spatial ring-1 | Tiles one tile-width beyond viewport in all directions. |
| 2 | Directional | If pan velocity is nonzero, 2-3 tiles ahead in the pan direction. |
| 3 | Zoom adjacent | Current viewport area at one zoom level above and below. |
| 4 | Background | Thumbnails for case panel, minimap updates. |

**Cancellation:** When the viewport changes, the prefetcher increments a generation counter. Worker threads check the generation before starting a decode; stale requests are dropped.

### 7.4 Memory Budget Summary

| Component | Single Slide | Multi-Slide (2-4) | Notes |
|-----------|-------------|-------------------|-------|
| CPU tile cache | 2 GB | 3-4 GB | 25% of RAM, clamped |
| GPU textures | 1 GB | 1.5-2 GB | 50% of detected GPU memory |
| Annotation model | < 50 MB | < 100 MB | Even thousands of annotations |
| Application + Qt | ~300 MB | ~400 MB | Widgets, framework overhead |
| SlideIO handles | ~100 MB | ~200 MB | Adapter pool per slide |
| **Total** | **~3.5 GB** | **~5-7 GB** | Within 4/8 GB targets |

---

## 8. Remote Image Loading

### 8.1 Architecture

Remote slides are accessed identically to local slides from the application's perspective, thanks to the `ISlideSource` abstraction. The `RemoteSlideClient` implements `ISlideSource` and adds a two-level cache (memory + disk).

```
Application  <-->  LruTileCache (memory)
                        |
                   [MISS]
                        v
                   DiskTileCache (persistent)
                        |
                   [MISS]
                        v
                   RemoteSlideClient  <-- HTTP/HTTPS -->  Tile Server
```

### 8.2 Authentication (Addressing Review Finding 5.4)

The `AuthManager` supports three authentication modes:

| Mode | Mechanism | Use Case |
|------|-----------|----------|
| Basic Auth | Username/password in HTTP Authorization header | Simple institutional servers |
| OAuth 2.0 | Bearer token from authorization server, with refresh | Enterprise SSO integration |
| Client Certificate | TLS client cert from OS keychain | High-security clinical environments |

Credentials are stored in the OS keychain (macOS Keychain, Windows Credential Manager, Linux Secret Service) -- never in plain text configuration files.

### 8.3 Disk Tile Cache

```
~/.slideio-viewer/cache/
    {server_hash}/
        {slide_id}/
            info.json              # cached metadata
            tiles/
                {level}_{x}_{y}.jpg
```

- **Default size limit:** 10 GB (configurable).
- **Eviction:** LRU by file access time. Cleaned on startup if over limit.
- **Cache-first lookup:** memory cache -> disk cache -> network fetch. Store in both caches on network fetch.

### 8.4 Connection Resilience

- On connection loss: status bar shows "Connection lost. Showing cached data." Cached tiles remain available at whatever resolution is cached.
- Auto-retry every 5 seconds with exponential backoff.
- On reconnection: resume tile loading seamlessly.
- Annotations continue to work locally during disconnection.

---

## 9. Annotation Storage

### 9.1 Content-Derived Slide Identifiers (Addressing Review Finding 1.5)

Slide filenames are not globally unique. The application generates a content-derived slide ID:

```cpp
std::string generateSlideId(const std::filesystem::path& path,
                            const SlideMetadata& meta) {
    // Hash of: file_size + dimensions + scan_date + scanner_id
    auto hash_input = std::to_string(std::filesystem::file_size(path))
        + std::to_string(meta.fullDimensions.width())
        + std::to_string(meta.fullDimensions.height())
        + meta.scanDate
        + meta.scannerModel;
    return sha256_hex(hash_input).substr(0, 16);  // 16-char hex ID
}
```

This ID is stored in the annotation file and verified on load. If the annotation file's embedded slide ID does not match the current slide's computed ID, the user is warned and given the option to re-associate or reject the annotations.

### 9.2 Annotation JSON Schema

```json
{
  "version": "1.0",
  "slideId": "a3f8c2e109b74d21",
  "slideFilename": "case001_HE.svs",
  "slideDimensions": { "width": 100000, "height": 80000 },
  "slideResolution": 0.25,
  "calibrationSource": "metadata",
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
        "coordinates": [[ [45230.5, 12890.0], [45500.0, 12890.0],
                          [45500.0, 13200.0], [45230.5, 13200.0],
                          [45230.5, 12890.0] ]]
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

**Coordinate system:** All geometry is in pixel units at pyramid level 0. Conversion to physical units: `physical_um = pixel * slideResolution`.

**Calibration source field (addressing review finding 1.4):** Records whether the resolution value came from slide metadata or manual user override. Measurements display a warning badge when calibration is manual or missing.

### 9.3 Geometry Type Mapping

| Annotation Type | Geometry Representation | Notes |
|----------------|------------------------|-------|
| Rectangle | `Polygon` (4 vertices + close) | Standard GeoJSON |
| Ellipse | `Ellipse` (center, radiusX, radiusY, rotation) | Extended type |
| Freehand Polygon | `Polygon` | Standard GeoJSON |
| Freehand Line | `LineString` | Standard GeoJSON |
| Point Marker | `Point` | Standard GeoJSON |
| Arrow | `Arrow` (tail, head) | Extended type |
| Ruler | `LineString` (2 points) + length property | Standard geometry, measurement in properties |
| Area Measurement | `Polygon` + area property | Standard geometry, measurement in properties |
| Angle Measurement | `Angle` (vertex, ray1End, ray2End) | Extended type |
| Text Label | `Point` + text property | Anchor point only |

### 9.4 File Locking (Addressing Review Finding 1.3)

On annotation load, the application acquires an advisory file lock on the annotation file:
- **Lock file:** `{annotation_file}.lock` containing PID, hostname, username, timestamp.
- On open: if a lock file exists and the owning process is still running (verified by PID check), warn the user: "Annotations are being edited by [user] on [host]. Open as read-only?"
- On close: release the lock file.
- Stale locks (owning process no longer running) are automatically cleaned up.

### 9.5 Incremental Save (Addressing Review Finding 4.1)

For slides with many annotations (10,000+), full JSON serialization on every auto-save is expensive. The incremental save strategy:

1. **Change log:** Each annotation mutation is appended to a `.annotations.wal` (write-ahead log) file as a compact JSON delta.
2. **Periodic compaction:** Every 5 minutes (or on explicit save), the full annotation set is written to the main `.annotations.json` file and the WAL is truncated.
3. **Recovery:** On load, if a WAL exists, replay it on top of the last compacted state.

This reduces auto-save I/O from O(N) to O(1) per annotation change.

### 9.6 Import/Export

| Format | Import | Export | Notes |
|--------|--------|--------|-------|
| Native JSON | Yes | Yes | Full fidelity |
| GeoJSON (QuPath) | Yes | Yes | Standard geometry, some property mapping |
| ASAP XML | Yes | No (v1) | Common legacy format |
| CSV (measurements) | No | Yes | Flat table: id, type, label, measurement, units |

**Export redaction (addressing review finding 5.1):** The "Export for sharing" function strips patient-identifying metadata (case metadata, author names containing real names) and optionally anonymizes annotation text labels.

### 9.7 Schema Versioning

The `"version"` field enables forward compatibility:
- New versions read all previous versions (backward compatible).
- On load, annotations in an older schema are automatically migrated and re-saved.
- Old versions encountering a newer schema display a warning and attempt best-effort loading.

---

## 10. Key Class Structures

### 10.1 Domain Layer: Core Types

```cpp
namespace slideio::viewer::core {

struct PointF { double x = 0.0, y = 0.0; };
struct SizeF  { double width = 0.0, height = 0.0; };
struct RectF  {
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    [[nodiscard]] bool contains(PointF p) const;
    [[nodiscard]] bool intersects(const RectF& other) const;
};
struct Color  { uint8_t r = 0, g = 0, b = 0, a = 255; };
struct TileSize { int width = 256, height = 256; };

enum class AnnotationType {
    Rectangle, Ellipse, FreehandPolygon, FreehandLine,
    PointMarker, Arrow, Ruler, AreaMeasurement,
    AngleMeasurement, TextLabel
};

struct TileKey {
    std::string slideId;
    int level = 0, tileX = 0, tileY = 0;
    bool operator==(const TileKey&) const = default;
};

} // namespace
```

### 10.2 Domain Layer: Annotation Geometry

```cpp
namespace slideio::viewer::core {

struct PolygonGeometry     { std::vector<PointF> vertices; };
struct EllipseGeometry     { PointF center; double radiusX, radiusY, rotation = 0.0; };
struct LineStringGeometry  { std::vector<PointF> points; };
struct PointGeometry       { PointF position; };
struct ArrowGeometry       { PointF tail, head; };
struct AngleGeometry       { PointF vertex, ray1End, ray2End; };

using AnnotationGeometry = std::variant<
    PolygonGeometry, EllipseGeometry, LineStringGeometry,
    PointGeometry, ArrowGeometry, AngleGeometry
>;

// Free functions for geometry operations
[[nodiscard]] RectF boundingBox(const AnnotationGeometry& geom);
[[nodiscard]] bool hitTest(const AnnotationGeometry& geom, PointF point, double tolerance);
[[nodiscard]] double computeArea(const AnnotationGeometry& geom);
[[nodiscard]] double computePerimeter(const AnnotationGeometry& geom);
[[nodiscard]] double computeLength(const AnnotationGeometry& geom);
[[nodiscard]] double computeAngle(const AngleGeometry& geom);  // degrees

} // namespace
```

### 10.3 Domain Layer: Annotation

```cpp
namespace slideio::viewer::core {

struct AnnotationProperties {
    std::string label;
    std::string classification;
    Color color{0xE6, 0x7E, 0x22};
    float lineWidth = 2.0f;
    float fillOpacity = 0.3f;
    std::string confidence;
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

    [[nodiscard]] const std::string& id() const;
    [[nodiscard]] AnnotationType type() const;
    [[nodiscard]] const AnnotationGeometry& geometry() const;
    [[nodiscard]] AnnotationGeometry& geometry();
    [[nodiscard]] const AnnotationProperties& properties() const;
    [[nodiscard]] const AnnotationMetadata& metadata() const;
    [[nodiscard]] const std::string& layerId() const;

    void setProperties(AnnotationProperties props);
    void setGeometry(AnnotationGeometry geom);
    void setLayerId(std::string layerId);

    [[nodiscard]] RectF boundingBox() const;
    [[nodiscard]] bool containsPoint(PointF slidePos, double tolerance) const;

private:
    std::string m_id;
    AnnotationType m_type;
    AnnotationGeometry m_geometry;
    AnnotationProperties m_properties;
    AnnotationMetadata m_metadata;
    std::string m_layerId;
};

} // namespace
```

### 10.4 Domain Layer: Interfaces

```cpp
namespace slideio::viewer::core {

class ISlideSource {
public:
    virtual ~ISlideSource() = default;
    virtual SlideMetadata metadata() const = 0;
    virtual int pyramidLevelCount() const = 0;
    virtual QSize levelDimensions(int level) const = 0;
    virtual double resolution() const = 0;
    virtual int channelCount() const = 0;
    virtual TileSize nativeTileSize() const = 0;
    virtual TileData readTile(int level, int tileX, int tileY,
                              int tileWidth, int tileHeight) const = 0;
    virtual QImage thumbnail(int maxWidth, int maxHeight) const = 0;
    virtual void close() = 0;
};

class ITileCache {
public:
    virtual ~ITileCache() = default;
    virtual std::optional<TileData> get(const TileKey& key) = 0;
    virtual void put(const TileKey& key, TileData data) = 0;
    virtual void evict(const std::string& slideId) = 0;
    virtual size_t cpuMemoryUsage() const = 0;
    virtual size_t gpuMemoryUsage() const = 0;
};

class IAnnotationRepository {
public:
    virtual ~IAnnotationRepository() = default;
    virtual AnnotationSet load(const std::string& slideId) = 0;
    virtual void save(const std::string& slideId, const AnnotationSet& annotations) = 0;
    virtual void exportTo(const std::string& slideId, const std::string& path,
                          ExportFormat format) = 0;
    virtual AnnotationSet importFrom(const std::string& path, ImportFormat format) = 0;
};

class IUserIdentity {
public:
    virtual ~IUserIdentity() = default;
    virtual std::string username() const = 0;
    virtual std::string displayName() const = 0;
    virtual bool isAuthenticated() const = 0;
};

} // namespace
```

### 10.5 Application Layer: ViewportController

```cpp
namespace slideio::viewer::app {

struct ViewportState {
    core::PointF center;
    double magnification = 1.0;
    double micronsPerPixel = 1.0;
    int viewportWidth = 0, viewportHeight = 0;
};

class ViewportController : public QObject {
    Q_OBJECT
public:
    explicit ViewportController(QObject* parent = nullptr);

    void setSlide(const core::TilePyramid& pyramid, double slideResolution);
    void setViewportSize(int width, int height);

    [[nodiscard]] core::PointF screenToSlide(core::PointF screenPos) const;
    [[nodiscard]] core::PointF slideToScreen(core::PointF slidePos) const;
    [[nodiscard]] core::RectF visibleSlideRect() const;

    void setCenter(core::PointF slidePos);
    void setMagnification(double magnification);
    void zoomAtScreenPoint(core::PointF screenPos, double factor);
    void fitToScreen();
    void panByScreenDelta(core::PointF delta);

    [[nodiscard]] int currentPyramidLevel() const;
    [[nodiscard]] std::vector<core::TileKey> visibleTileCoords(
        const std::string& slideId) const;

    [[nodiscard]] ViewportState state() const;

signals:
    void viewChanged();
    void magnificationChanged(double magnification);

private:
    ViewportState m_state;
    core::TilePyramid m_pyramid;
    double m_slideResolution = 0.25;
};

} // namespace
```

### 10.6 Application Layer: AnnotationModel

```cpp
namespace slideio::viewer::app {

class AnnotationModel : public QObject {
    Q_OBJECT
public:
    explicit AnnotationModel(QObject* parent = nullptr);

    // CRUD
    void addAnnotation(std::unique_ptr<core::Annotation> annotation);
    void removeAnnotation(const std::string& id);
    void updateAnnotation(const std::string& id, core::AnnotationProperties props);

    // Query
    [[nodiscard]] core::Annotation* findById(const std::string& id) const;
    [[nodiscard]] std::vector<core::Annotation*> findInRect(core::RectF slideRect) const;
    [[nodiscard]] std::vector<core::Annotation*> findByLayer(const std::string& layerId) const;
    [[nodiscard]] std::vector<core::Annotation*> search(const std::string& query) const;
    [[nodiscard]] core::Annotation* hitTest(core::PointF slidePos, double tolerance) const;

    // Layers
    void addLayer(const std::string& name);
    void removeLayer(const std::string& id);
    void setLayerVisible(const std::string& id, bool visible);
    void setLayerLocked(const std::string& id, bool locked);
    [[nodiscard]] std::vector<core::AnnotationLayer> layers() const;

signals:
    void annotationAdded(const std::string& id);
    void annotationRemoved(const std::string& id);
    void annotationChanged(const std::string& id);
    void layerChanged(const std::string& layerId);
    void modelModified();
};

} // namespace
```

### 10.7 Application Layer: UndoRedoStack

```cpp
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

    std::function<void()> onStackChanged;

private:
    std::vector<std::unique_ptr<ICommand>> m_commands;
    int m_currentIndex = -1;
    int m_maxDepth;
};

} // namespace
```

### 10.8 Infrastructure Layer: TileLoadScheduler

```cpp
namespace slideio::viewer::infra {

enum class TilePriority : int {
    Visible = 0, SpatialPrefetch = 1,
    DirectionalPrefetch = 2, ZoomPrefetch = 3, Background = 4
};

struct TileRequest {
    core::TileKey key;
    TilePriority priority;
    uint64_t sequenceNumber;
    uint64_t generation;        // viewport generation for cancellation

    bool operator>(const TileRequest& other) const;
};

class TileLoadScheduler {
public:
    TileLoadScheduler(core::ITileCache& cache, int workerThreadCount);
    ~TileLoadScheduler();

    void setSlideSource(std::shared_ptr<core::ISlideSource> source);
    void requestTile(const core::TileKey& key, TilePriority priority);
    void requestTiles(const std::vector<core::TileKey>& keys, TilePriority priority);
    void cancelAllPending();
    void cancelForSlide(const std::string& slideId);
    void shutdown();

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
    std::atomic<uint64_t> m_generation{0};
    std::atomic<uint64_t> m_sequenceCounter{0};
};

} // namespace
```

### 10.9 Presentation Layer: ViewportWidget

```cpp
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
    void tileLoadingStateChanged(bool loading);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void renderTiles();
    void renderAnnotations();
    void renderOverlays();
    void uploadPendingTextures(int maxPerFrame = 8);  // PBO async upload

    app::ViewportController* m_viewportController = nullptr;
    app::AnnotationModel* m_annotationModel = nullptr;
    DrawingToolController* m_drawingTools = nullptr;

    std::unique_ptr<QOpenGLShaderProgram> m_tileShader;
    std::unique_ptr<QOpenGLShaderProgram> m_annotationShader;
    GLuint m_tileVAO = 0, m_tileVBO = 0;
    std::array<GLuint, 2> m_pboIds{};  // double-buffered PBO for async upload

    std::unordered_map<core::TileKey, std::unique_ptr<QOpenGLTexture>> m_textures;

    bool m_isPanning = false;
    bool m_spaceHeld = false;
    QPoint m_lastMousePos;
};

} // namespace
```

---

## 11. Threading Model

### 11.1 Thread Architecture

```
+------------------------------------------------------------------+
|                         UI Thread (main)                          |
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
|  Monitors viewport state via atomic snapshot                      |
|  Enqueues proactive tile requests for adjacent tiles              |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                     Auto-Save Thread                              |
|  Takes annotation snapshot on UI thread                           |
|  Serializes to JSON and writes to disk on its own thread          |
|  Debounced: 2s after last change + periodic 30s                   |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                   Network I/O (Qt internal)                       |
|  QNetworkAccessManager for HTTP tile fetch (remote slides only)   |
+------------------------------------------------------------------+
```

### 11.2 Thread Pool Sizing

```cpp
int workerCount() {
    int cores = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(2, cores - 2);  // reserve for UI + prefetch/autosave
}
```

On a typical 8-core clinical workstation: 6 tile decode workers. Tile decoding is CPU-bound (JPEG/JPEG2000 decompression), so saturating available cores is appropriate.

### 11.3 Synchronization Strategy

| Shared Resource | Protection | Rationale |
|----------------|-----------|-----------|
| `LruTileCache` | `std::shared_mutex` (readers-writer) | Many concurrent reads, infrequent writes |
| `TileLoadScheduler` queue | `std::mutex` + `std::condition_variable` | Producer-consumer; workers wait on CV |
| `SlideIOAdapterPool` | `std::mutex` + `std::condition_variable` | Workers acquire/release adapters |
| `AnnotationModel` | UI thread affinity (no locking) | All mutations on UI thread; workers never touch |
| `AutoSaver` | Snapshot on UI thread, serialize on save thread | No locks held during file I/O |
| `ViewportController` state | UI thread affinity + `std::atomic<ViewportState>` for prefetcher | Prefetcher reads atomic snapshot, never mutates |

### 11.4 Thread-to-UI Communication

Worker threads signal the UI thread via `Qt::QueuedConnection`:

```cpp
void TileDecodeWorker::onTileDecoded(const TileKey& key, TileData data) {
    m_cache.put(key, std::move(data));
    QMetaObject::invokeMethod(m_viewport, [this, key]() {
        m_viewport->onTileReady(key);
    }, Qt::QueuedConnection);
}
```

This ensures OpenGL texture uploads and widget repaints happen exclusively on the UI thread.

---

## 12. Plugin Architecture

### 12.1 Extension Points

| Extension Point | Description | Example |
|----------------|-------------|---------|
| `analysis.run` | Run analysis on current slide/viewport | AI tumor detection overlay |
| `annotation.type` | Register custom annotation type | Cell counter |
| `annotation.classifier` | Auto-classify annotations | ML tissue classifier |
| `export.format` | Register custom export format | DICOM SR export |
| `import.format` | Register custom import format | Proprietary import |
| `toolbar.action` | Add toolbar button | Custom analysis tool |
| `menu.action` | Add menu item | Institutional integration |
| `panel.custom` | Add side panel | Analysis results panel |
| `slide.source` | Register custom slide source | Cloud storage, LDAP auth |
| `viewport.overlay` | Draw custom viewport overlays | Heatmap overlay |

### 12.2 Plugin API

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual PluginInfo info() const = 0;
    virtual bool initialize(PluginContext& context) = 0;
    virtual void shutdown() = 0;
};

constexpr int PLUGIN_API_VERSION_MAJOR = 1;
constexpr int PLUGIN_API_VERSION_MINOR = 0;

// C factory function exported by plugin shared libraries
extern "C" IPlugin* createPlugin();
extern "C" int pluginApiVersionMajor();
extern "C" int pluginApiVersionMinor();
```

### 12.3 PluginContext

```cpp
class PluginContext {
public:
    // Slide access (read-only)
    const ISlideSource* currentSlide() const;
    TileData readTile(int level, int x, int y, int w, int h) const;

    // Viewport (read, request navigation)
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

    // Progress and logging
    void reportProgress(double fraction, const std::string& message);
    void log(LogLevel level, const std::string& message);
};
```

### 12.4 Plugin Safety (Addressing Review Finding 4.3)

- **Code signing:** Plugins can optionally be verified against a code-signing certificate. Unsigned plugins trigger a security warning on first load, requiring explicit user approval.
- **Plugin allowlist:** Institutional IT can configure an allowlist of approved plugins.
- **Auto-disable:** If a plugin crashes the application twice (tracked in a crash log), it is automatically disabled on next startup.
- **Safe mode:** Launching with `--safe-mode` disables all plugins.
- **Execution timeouts:** Long-running plugin operations (AI analysis) must use the progress API. Operations exceeding a configurable timeout (default 5 minutes) are cancelled.
- **Process isolation (v2):** AI analysis plugins that run long computations will execute in a separate process with IPC, preventing them from crashing the main viewer.

---

## 13. Security and Identity

### 13.1 User Identity (Addressing Review Finding 1.1)

The `IUserIdentity` interface provides annotation authorship:

**Tier 1 (default):** Username configured in application preferences. Required on first launch. Stored in `QSettings`. The annotation `author` field is derived from this, not user-editable per annotation.

**Tier 2 (institutional):** LDAP/Active Directory or SSO integration via the plugin extension point `slide.source`. The identity plugin provides authenticated username and display name.

### 13.2 Privacy Protections (Addressing Review Findings 5.1, 5.2)

- **Slide label images:** Hidden by default. Revealed only by explicit user action (View > Show Label Image). Automatically hidden in presentation/full-screen mode.
- **Export redaction:** "Export for sharing" strips patient identifiers from case metadata and annotation text. Configurable redaction rules.
- **Annotation encryption:** For institutional deployments, annotation files can optionally be encrypted at rest (AES-256-GCM, key from institution-managed keystore).

### 13.3 Audit Logging (Addressing Review Finding 5.5)

Structured audit events logged to `~/.slideio-viewer/audit/audit.log`:

```json
{"timestamp":"2026-03-13T14:30:00Z","event":"slide.open","user":"dr.smith","slideId":"a3f8c2e1","path":"/data/case001_HE.svs"}
{"timestamp":"2026-03-13T14:35:00Z","event":"annotation.create","user":"dr.smith","slideId":"a3f8c2e1","annotationId":"ann-uuid-1","type":"polygon"}
{"timestamp":"2026-03-13T14:40:00Z","event":"annotation.export","user":"dr.smith","slideId":"a3f8c2e1","format":"geojson","path":"/export/annotations.geojson"}
```

- Append-only file (no editing of past entries).
- For institutional deployments: forward audit events to external SIEM via syslog or webhook plugin.

---

## 14. Technology Choices

### 14.1 Core Technology Stack

| Technology | Choice | Rationale |
|-----------|--------|-----------|
| **Language** | C++17, select C++20 | Performance-critical; C++17 widely supported; C++20 for `std::jthread`, concepts where available |
| **UI Framework** | Qt 6 Widgets + QOpenGLWidget | Mature cross-platform; QDockWidget for detachable panels; QOpenGLWidget for GPU rendering |
| **Slide Library** | SlideIO (pinned version) | Multi-format WSI access; adapter pattern isolates API changes |
| **Rendering** | OpenGL 3.3+ behind `ITileRenderer` abstraction | Sufficient for 2D tile compositing; widely supported; abstraction enables future Metal/Vulkan |
| **Build System** | CMake 3.21+ | Industry standard; Qt 6 integration; cross-platform |
| **Package Manager** | Conan 2 | Cross-platform dependency management |
| **JSON** | nlohmann/json | Ergonomic API for annotation serialization |
| **Logging** | spdlog | Structured logging with rotation |
| **Testing** | Catch2 3.x | Unit and integration testing |
| **Benchmarking** | Google Benchmark | Cache and rendering performance |

### 14.2 Why Qt Widgets over QML

1. `QDockWidget` for detachable panels -- no QML equivalent.
2. `QOpenGLWidget` for direct OpenGL context -- QML Scene Graph adds overhead.
3. Desktop-first application -- QML's touch/mobile strengths not needed.
4. Easier debugging with standard C++ tools.

### 14.3 Renderer Abstraction (Addressing Review Finding 6.3)

OpenGL is chosen for v1, but the rendering layer is abstracted behind an `ITileRenderer` interface:

```cpp
class ITileRenderer {
public:
    virtual ~ITileRenderer() = default;
    virtual void initialize() = 0;
    virtual void resize(int width, int height) = 0;
    virtual void uploadTile(const TileKey& key, const TileData& data) = 0;
    virtual void releaseTile(const TileKey& key) = 0;
    virtual void renderFrame(const ViewportState& state,
                             const std::vector<TileKey>& visibleTiles) = 0;
    virtual void renderAnnotations(const std::vector<Annotation*>& annotations,
                                   const ViewportState& state) = 0;
};
```

The current implementation is `OpenGLTileRenderer`. A `MetalTileRenderer` (for macOS post-OpenGL deprecation) or `RhiTileRenderer` (using Qt 6's RHI abstraction) can be substituted without changing the rest of the application.

### 14.4 Build and CI

- CMake with `FetchContent` for header-only libraries, Conan for compiled dependencies.
- CI: GitHub Actions with matrix builds (Windows MSVC 2022, macOS AppleClang, Ubuntu 22.04 GCC 12).
- Sanitizers: AddressSanitizer on Linux/macOS in debug builds.
- Static analysis: clang-tidy with project `.clang-tidy` configuration.
- Style: clang-format with project `.clang-format` configuration.
- Packaging: CPack (NSIS on Windows, DMG on macOS, AppImage on Linux).

---

## 15. Cross-Cutting Concerns

### 15.1 Error Handling

- **Domain layer:** Uses `std::expected<T, Error>` (C++23 or polyfill) for recoverable errors.
- **Infrastructure layer:** Translates external exceptions (SlideIO, file I/O, network) to application error types at the adapter boundary. No exceptions escape infrastructure.
- **Presentation layer:** Non-modal notification banners for non-critical errors. Modal dialogs only for fatal/blocking errors (e.g., "Cannot open slide file").

### 15.2 Configuration

- `QSettings` for platform-native storage (registry on Windows, plist on macOS, ini on Linux).
- Settings: memory budget, tile cache size, UI layout, theme, shortcuts, annotation colors, plugin paths, user identity, remote server credentials (reference to OS keychain).
- Accessible via `ConfigService` injected into components.

### 15.3 Logging

- `spdlog` with three sinks:
  1. **Rotating file** (debug level, 10 MB x 5 files) — persistent log at `~/.slideio-viewer/logs/viewer.log`.
  2. **Console** (info level, debug builds only) — stderr output for development.
  3. **Ring-buffer sink** (debug level, 10,000 entries) — in-memory buffer backing the Application Log panel in the UI. The `LogPanelWidget` reads from this sink and displays entries in real time. The sink is always active regardless of whether the panel is visible, so entries are available when the user opens the panel.
- Format: `[2026-03-13 14:30:00.123] [info] [TileCache] Cache hit ratio: 94.2%`.
- The `LogPanelWidget` connects to the ring-buffer sink via a Qt signal bridged from spdlog's custom sink callback, ensuring thread-safe delivery to the UI thread.

### 15.4 Session Recovery

- On startup, check for `.session.json` (written on every slide open/close, updated every 60 seconds).
- If present and the application did not exit cleanly: restore open slides, viewport positions, and unsaved annotations from the auto-save WAL.
- Session file is deleted on clean exit.

### 15.5 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Frame rate during pan/zoom | >= 60 fps | Tracy frame time profiler |
| Tile decode latency (JPEG) | < 5 ms per tile | Tracy zone measurement |
| Cache lookup time | < 1 us | Google Benchmark |
| Time to first tile on slide open | < 500 ms (local) | Manual timing + Tracy |
| Slide open to first view | < 2 s (local), < 5 s (remote) | End-to-end measurement |
| Annotation creation | < 100 ms response | Input-to-render latency |
| Memory under single-slide load | < 4 GB | RSS monitoring |

### 15.6 Multi-Data-Type Display

The rendering pipeline supports all pixel data types provided by SlideIO: 8-bit (unsigned/signed), 16-bit (unsigned/signed), 32-bit integer, 64-bit integer, and floating-point (16/32/64-bit).

**OpenGL texture format mapping:**

| Data Type | GL Internal Format | GL Type | Strategy |
|-----------|-------------------|---------|----------|
| Byte (uint8) | `GL_R8` / `GL_RGB8` | `GL_UNSIGNED_BYTE` | Normalized [0,1] |
| Int8 | `GL_R8_SNORM` / `GL_RGB8_SNORM` | `GL_BYTE` | Normalized [-1,1] |
| UInt16 | `GL_R16` / `GL_RGB16` | `GL_UNSIGNED_SHORT` | Normalized [0,1] |
| Int16 | `GL_R16_SNORM` / `GL_RGB16_SNORM` | `GL_SHORT` | Normalized [-1,1] |
| Float16 | `GL_R16F` / `GL_RGB16F` | `GL_HALF_FLOAT` | Raw float |
| Float32 | `GL_R32F` / `GL_RGB32F` | `GL_FLOAT` | Raw float |
| UInt32, Int32, Int64, UInt64, Float64 | `GL_R32F` / `GL_RGB32F` | `GL_FLOAT` | CPU-convert to float32 |

**Display range auto-detection:** On slide open, the coarsest pyramid level is scanned to compute global min/max across all channels. This range is stored in `SlideInfo::displayRange` and passed to the fragment shader as `uDisplayMin`/`uDisplayMax` uniforms. The shader maps `[min, max] → [0, 1]` via linear contrast: `mapped = clamp((value - min) / (max - min), 0, 1)`.

For normalized GL formats (uint8, uint16), the raw min/max are converted to sampler output space (e.g., uint16 range 0–65535 maps to 0.0–1.0) before being passed as uniforms.

**Thumbnail generation:** The minimap thumbnail converts non-8-bit pixel data to uint8 using the detected display range, ensuring correct display for all data types.

### 15.7 Channel Mixing (Fluorescence Rendering)

The viewer uses two rendering paths based on slide type:

**Brightfield detection:** A slide is classified as brightfield if `numChannels == 1` or `(numChannels == 3 && channelDataType == Byte)`. All other slides are treated as fluorescence/multi-channel.

**Brightfield path:** Single texture per tile, standard alpha blending, RGB passthrough. The channel mixer panel is hidden.

**Fluorescence path:** One GL_RED texture per channel per tile, multi-pass additive blending with pseudo-colors:

1. **Texture upload:** The interleaved tile buffer is split into per-channel single-channel (GL_RED) textures at upload time. Each channel gets its own OpenGL texture object.

2. **Rendering:** For each tile, each visible channel is rendered as a separate draw call:
   - Background cleared to black `(0, 0, 0)` for correct additive compositing
   - Blending mode: `glBlendFunc(GL_ONE, GL_ONE)` — pure additive
   - The channel fragment shader applies: `color = channelColor * clamp((value - min) / (max - min), 0, 1)`

3. **Per-channel display range:** Each channel has independent `DisplayRange` (min/max), auto-detected from the coarsest pyramid level during slide open.

4. **Default pseudo-colors:** Assigned by matching channel names from SlideIO metadata against known fluorescence dye names (DAPI→blue, FITC→green, Cy3→red, Cy5→magenta). Falls back to index-based defaults for unnamed channels.

**Channel Mixer Panel:** A `QDockWidget` providing per-channel controls:
- Visibility toggle (QCheckBox) per channel
- Pseudo-color picker (QPushButton → QColorDialog) per channel
- Channel name label
- Dockable, floatable, closable; position persisted via `QMainWindow::saveState()`/`restoreState()` with `QSettings`
- Hidden for brightfield slides; shown automatically for fluorescence slides

### 15.8 Multi-Scene Support

SlideIO slides can contain multiple scenes (image regions) and auxiliary images (labels, macro overviews). The viewer supports browsing and switching between them.

**SceneInfo struct** in `Types.h` stores per-scene metadata: index, name, dimensions, channel count, and auxiliary image name (for aux images). `SlideInfo` carries `scenes` and `auxImages` vectors populated on slide open.

**SlideIOAdapter** accepts a scene index or auxiliary image name parameter. `enumerateScenes()` is a static method that reads all scene and auxiliary image metadata from a slide file without loading tile data.

**SceneThumbnailPanel** is a `QDockWidget` showing clickable thumbnails:
- Scene thumbnails (from coarsest pyramid level) with names below
- Separator line
- Auxiliary image thumbnails with names below
- Active scene highlighted with colored border (`#4A90D9`)
- Thumbnail size configurable via QSettings (`sceneThumbnails/size`, default 256)
- Position persisted via `QMainWindow::saveState()`/`restoreState()`

**Scene switching** tears down the current tile pipeline (`closeSlide()`) and rebuilds it for the selected scene index or auxiliary image name. The thumbnail panel is not rebuilt on switch — only the highlight changes.

---

*This document provides the complete software architecture and design for SlideIO Viewer, incorporating findings from all five design phases including the critical review. It serves as the definitive technical reference for implementation.*
