# Implementation Stages

**Project:** SlideIO Viewer -- Cross-Platform Pathology Slide Viewer
**Date:** 2026-04-11

---

## Overview

The implementation is organized into three major releases with eight stages. Each stage delivers a user-facing milestone. Testing is continuous -- not a separate phase.

### Release Summary

| Release | Stages | Scope |
|---------|--------|-------|
| Release 1 | Stages 1-4 | Core viewer: open, navigate, annotate, case management |
| Release 2 | Stages 5-6 | Remote access, plugin system |
| Release 3 | Stages 7-8 | Python scripting, image analysis, script debugging |

---

## Release 1: Core Viewer (Stages 1-4)

Delivers a usable slide viewer -- open, navigate, and view slides locally with full annotation support.

### Stage 1 -- Project Scaffolding & Slide Loading

1. CMake structure with 4 library targets (`core`, `app`, `infra`, `ui`), Conan setup, CI pipeline (GitHub Actions, 3 platforms).
2. `.clang-format`, `.clang-tidy`, Catch2 test harness.
3. Core domain types: `Types.h`, `TileKey`, `TileData`, `TilePyramid`, `Viewport`, `CoordinateSystem`.
4. `SlideIOAdapter`: open slide, read metadata, decode tiles.
5. `LruTileCache`: insert, lookup, evict, memory budget.
6. Basic `MainWindow` with file open dialog.
7. Basic `ViewportWidget`: render tiles from cache with OpenGL.
8. Unit tests for core types and cache.

**Milestone:** Open a local slide file and display the lowest resolution level on all 3 platforms.

### Stage 2 -- Navigation & Tile Pipeline

1. `ViewportController`: coordinate transforms, zoom levels, pan, fit-to-screen, actual pixels.
2. Mouse/keyboard input handling (scroll-zoom, click-drag pan, arrow keys).
3. `SlideIOAdapterPool` for parallel tile decoding.
4. `TileLoadScheduler` with priority queue and thread pool.
5. Progressive tile refinement (low-res placeholder to high-res crossfade).
6. `Prefetcher` (spatial ring, zoom level, directional).
7. `MinimapWidget`, `ZoomIndicatorWidget`.
8. `StatusBarManager` (magnification, coordinates, scale bar).
9. Performance benchmarks for tile decode and cache.

**Milestone:** Smooth 60fps pan/zoom on gigapixel slides. Minimap and zoom indicator functional.

### Stage 3 -- Annotations

1. `Annotation`, `AnnotationGeometry` (all variant types), `AnnotationLayer`.
2. `AnnotationModel` with CRUD, spatial queries, signals.
3. `DrawingToolController` for all annotation tools (rectangle, ellipse, freehand, point, arrow, ruler, area, text, angle).
4. Annotation rendering overlay in `ViewportWidget`.
5. `UndoRedoStack` with command pattern.
6. `AnnotationListPanel`, `PropertiesPanel`, `LayerPanel`.
7. `JsonAnnotationSerializer` with schema versioning.
8. `AutoSaver` (debounced 2s + periodic 30s).
9. Annotation import (QuPath GeoJSON, ASAP XML) and export (GeoJSON, CSV).
10. Measurement calibration with manual override, warning badges.

**Milestone:** Full annotation workflow -- create, edit, undo/redo, save, reload, import/export. Measurements with calibrated units.

### Stage 4 -- Case Management & Polish

1. `Case`, `CaseService`, `case.json` manifest format.
2. `SlideTrayPanel` with thumbnails, grouping, search/filter.
3. `SplitViewController` (1x1, 2x1, 1x2, 2x2 with synchronized navigation).
4. `BookmarkService` and navigation history (back/forward).
5. `SnapshotService` for viewport export.
6. `ThemeManager` (light, dark, system).
7. `ShortcutManager` with full keyboard shortcut scheme.
8. User identity (username/initials in preferences).
9. Audit logging (structured log of slide open, annotation CRUD, exports).
10. Application Log panel (`LogPanelWidget` with ring-buffer sink).
11. Session recovery (`.session.json`, auto-save WAL).
12. Multi-monitor support (detachable dock widgets, saved layouts).
13. Cross-platform packaging (NSIS, DMG, AppImage).

**Milestone:** Complete clinical workflow -- open case, review slides, annotate, compare, bookmark, export, session recovery. **Release 1 candidate.**

---

## Release 2: Connectivity & Extensibility (Stages 5-6)

Adds remote access and the plugin system for institutional and advanced use.

### Stage 5 -- Remote Slide Access

1. `RemoteSlideClient` (HTTP/HTTPS tile server client).
2. `DiskTileCache` (~10 GB persistent cache for remote tiles).
3. Authentication: Basic Auth, OAuth 2.0, client certificates, OS keychain integration.
4. Connection handling: auto-retry, timeout, rate limiting, request coalescing.
5. Offline mode with cached tiles.
6. Slide label PHI handling (default hidden, explicit enable).

**Milestone:** Open and navigate remote slides with authentication. Seamless fallback to disk cache on network loss.

### Stage 6 -- Plugin System

1. `PluginLoader` (shared library loading, version verification, code signing).
2. `PluginContext` (stable versioned API).
3. Plugin lifecycle: timeout monitoring, memory tracking, auto-disable on crash, safe mode.
4. Separate process for long-running plugin computations.
5. Example plugin with documentation.
6. Plugin configuration UI.

**Milestone:** Third-party plugins can be loaded, run analysis, and contribute results. **Release 2 candidate.**

---

## Release 3: Scripting & Image Analysis (Stages 7-8)

Adds the embedded Python scripting engine and image analysis capabilities.

### Stage 7 -- Python Scripting Engine

1. Embed Python interpreter (pybind11 or similar).
2. Background thread execution with GIL management, cancel support.
3. Script console (REPL) with output/error display.
4. Built-in script editor with Python syntax highlighting.
5. Scripting API:
   - Slide access: metadata, pixel data as NumPy arrays.
   - Annotation CRUD with real-time viewport updates.
   - Viewport control: pan, zoom, go to coordinates.
   - Case/slide iteration, batch operations.
   - Measurement/calibration data access.
   - Annotation layer management.
   - Progress reporting to UI.
   - Log writing.
6. Script file execution from menu and console.
7. Script directories configuration, recent scripts list.

**Milestone:** Interactive Python scripting -- read slide data, create annotations from script, see results live in viewport.

### Stage 8 -- Debugging & Image Analysis Workflows

1. Script debugger: breakpoints, step-over, step-into, step-out.
2. Variable inspection panel, call stack display.
3. Breakpoint management in the script editor.
4. Image analysis workflow templates/examples (e.g., tissue detection, cell counting).
5. Third-party package support verification (NumPy, scikit-image, OpenCV, PyTorch).
6. Documentation and example scripts.

**Milestone:** Users can write, debug, and share Python analysis scripts. **Release 3 candidate.**

---

## Testing Strategy

Testing is continuous across all stages, not a separate phase.

| Activity | When |
|----------|------|
| Unit tests (Catch2) | Written alongside every component, gate every PR |
| Format compatibility suite | Established in Stage 1, run on every CI build |
| Performance benchmarks | Established in Stage 2, tracked for regressions |
| Integration tests | Added per stage (cache + adapter, serializer + file I/O) |
| Cross-platform CI | From Stage 1 -- every commit builds on all 3 platforms |
| Manual clinical workflow testing | End of Stages 3, 4, and each release |

---

## Team Allocation by Stage

| Stage | Lead Dev | UI/OpenGL Dev | Infra Dev | Python Dev |
|-------|----------|---------------|-----------|------------|
| 1 | Architecture, core types | ViewportWidget, OpenGL | SlideIOAdapter, cache | -- |
| 2 | ViewportController, tests | Minimap, zoom, input | Tile scheduler, prefetcher, pool | -- |
| 3 | Annotation domain, undo | Drawing tools, rendering | Serializer, auto-save, import/export | -- |
| 4 | Case model, session recovery | Panels, themes, shortcuts | Audit logging, log panel | -- |
| 5 | Architecture review | Offline mode UI | Remote client, disk cache, auth | -- |
| 6 | Plugin API design | Plugin config UI | Plugin loader, process isolation | -- |
| 7 | API design, review | Script console, editor UI | -- | Python embedding, scripting API |
| 8 | -- | Debugger UI panels | -- | Debugger engine, examples |

---

## Key Risks

| Stage | Risk | Mitigation |
|-------|------|------------|
| 1 | SlideIO integration issues with specific formats | Build format compatibility suite early, test with real clinical slides |
| 2 | 60fps target on older hardware | Profile early, budget GPU texture uploads per frame (PBO, 4-8 limit) |
| 3 | Annotation hit-testing performance with many annotations | Spatial index (R-tree) if >1000 annotations per slide |
| 4 | Qt layout complexity with split view + dockable panels | Prototype split view early in Stage 2 |
| 5 | Network latency masking -- perceived performance | Two-level cache (memory to disk), aggressive prefetch |
| 7 | Python GIL contention with UI thread | Dedicated Python thread, release GIL during C++ calls, Qt signal bridge for UI updates |
| 8 | Debugger complexity | Consider integrating debugpy rather than building from scratch |
