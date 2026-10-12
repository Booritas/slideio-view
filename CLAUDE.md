# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SlideIO Viewer is a cross-platform C++ desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It uses the **SlideIO library** for reading image data and **Qt 6** for the UI.

**Current state:** Implemented and building. The four layer libraries, the Qt application, a CMake/Conan build and four ctest suites all exist under `src/` and `tests/`. The viewer opens slides, navigates a tile pyramid on OpenGL, manages ICC colour profiles, and supports rectangle, polygon and freehand-line annotations (draw, select, move, delete) that **persist to disk** — one JSON file per (slide, scene) in a workspace folder under Documents, autosaved and reloaded when the slide reopens.

Annotation persistence has **never been exercised in the running application** — no test suite creates a `QApplication`, so no widget can be constructed in a test. See item 0 of `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md` for the checks that still need a human and a real slide.

`documents/` holds the original design, much of which is still ahead of the code. Treat it as the plan, not a description of what is there; `documents/implementation-stages.md` tracks which stages have landed. Where a design document and the code disagree, the code is the truth — several design details turned out to be unimplementable as written.

**Not built yet:** vertex editing and the ellipse, circle, point, arrow and text shapes, annotation panels (list, properties, layers), import/export, advisory file locking, undo/redo, cases, bookmarks, split view, remote slides, the plugin system, and Python scripting.

## Design Documents

All specifications live under `documents/`. Read them in this order for context:

1. **`01-software-requirements-specification.md`** — Functional/non-functional requirements, user personas, clinical workflows
2. **`02-user-interface-design.md`** — UI layout, interaction patterns, keyboard shortcuts, annotation workflows
3. **`03-software-architecture-and-design.md`** — Layered architecture, component design, data flows, class structures, threading model
4. **`phase4-implementation-strategy.md`** — C++ coding standards, project structure, CMake targets, example class definitions
5. **`phase5-critical-review.md`** — Known design issues, performance risks, and recommended mitigations

Supporting discovery documents (`phase1-requirements-discovery.md`, `phase2-ux-concept.md`, `phase3-system-architecture.md`) contain the iterative design discussion. `claude-team.md` is the multi-agent prompt that generated the design.

## Architecture

**Layered, strict downward dependency.** Components in _italics_ are designed but not built.

```
Presentation (Qt UI)   →  MainWindow, ViewportWidget, PreferencesDialog,
                          AnnotationSettings, Panels, dialogs                  (25 .cpp)
Application (Services) →  AnnotationModel, AnnotationPersistenceService,
                          SlideViewerService, _CaseService_                     (3 .cpp)
Domain (Core Logic)    →  Annotation, AnnotationSerialization, AutosavePolicy,
                          Iso8601, Viewport, TilePyramid, CoordinateSystem     (16 .cpp)
Infrastructure         →  SlideIOAdapter, LruTileCache, TileLoadScheduler,
                          JsonAnnotationRepository,
                          QSettingsColorProfileOverrideStore, _RemoteClient_    (6 .cpp)
```

- Core is **pure C++17, Qt-free** — enforced by it linking no Qt target at all. Anything needing `QUuid`, `QSettings` or signals belongs one layer up. It links `nlohmann_json` PRIVATE, so JSON appears in no core public header.
- Infrastructure implements core interfaces (`ISlideSource`, `ITileCache`, `IAnnotationRepository`).
- SlideIO access is isolated behind `SlideIOAdapter`. There is **no** `SlideIOAdapterPool` — one shared scene serves all tile workers; see the project memory on scene sharing.

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++17 |
| UI | Qt 6 Widgets |
| Rendering | OpenGL 3.3 core profile; `src/ui/resources/shaders/tile.{vert,frag}` |
| Slide I/O | SlideIO, built from the `extern/slideio` submodule |
| Build | CMake 3.20+, Conan 2 |
| Tests | Catch2 v3, four ctest suites (core, app, infra, ui) |
| Logging | spdlog |
| Compilers | MSVC 2022, GCC 12+, AppleClang 14+ |

nlohmann_json is linked **PRIVATE to `slideio-viewer-core`** and used only by `AnnotationSerialization.cpp`; it must never appear in a core public header. The serializer uses `nlohmann::ordered_json`, not `nlohmann::json` — the default sorts keys alphabetically, which buries `slideId` and `schemaVersion` below the whole annotation array in a format chosen to be read in a text editor. Qt6::Network is still unused.

## Coding Conventions

```
Classes/Structs:    PascalCase          (TileCache, AnnotationModel)
Functions/Methods:  camelCase           (readTile, setZoomLevel)
Member variables:   m_camelCase         (m_memoryBudget, m_tileCache)
Constants:          kPascalCase         (kDefaultTileSize, kMaxZoomLevel)
Enums:              PascalCase::Value   (AnnotationType::Polygon)
Namespaces:         lowercase           (slideio::viewer::core)
Files:              PascalCase.h/.cpp   (TileCache.h, TileCache.cpp)
```

- 4-space indent, 120-char line limit
- Allman braces for classes/functions, K&R for control flow
- `#pragma once` for header guards
- Include order: own header, project headers, Qt headers, std headers
- PIMPL idiom for Qt widget classes

## CMake Targets

| Target | Links Against |
|--------|--------------|
| `slideio-viewer-core` | nothing (no Qt, by design) |
| `slideio-viewer-app` | core, Qt6::Core |
| `slideio-viewer-infra` | core (PUBLIC); SlideIO, spdlog, Qt6::Core (PRIVATE) |
| `slideio-viewer-ui` | core, app, infra, Qt6::Widgets, Qt6::OpenGLWidgets |
| `slideio-viewer` | ui, spdlog |

`CMAKE_AUTOMOC` and `CMAKE_AUTORCC` are on globally. AUTOMOC does not scan a header that lives in a different directory from its `.cpp`, so a `Q_OBJECT` header under `include/` must be listed in its target's sources — `src/app/CMakeLists.txt` does this explicitly, `src/ui/CMakeLists.txt` with a glob.

## Build and Test

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
cmake --install build/build --config Release
```

**Never run a Qt-linked test executable directly.** `app-tests`, `infra-tests` and `ui-tests` find their DLLs through a ctest `ENVIRONMENT_MODIFICATION` property that only applies under `ctest`; running the `.exe` pops a modal Windows error box that blocks until a human dismisses it. Only `core-tests` is safe to run directly.

The build-tree executable will not start either — install first and run `build/install/release/bin/slideio-viewer.exe`.

No test binary creates a `QApplication`, so **no test may construct a widget**. Logic that needs testing is extracted into pure functions over plain data; see `ColorProfilePolicy`, `AnnotationInteraction`, `DriverFilters`.

## Key Design Decisions

Shipped:

- **Content-derived slide IDs** — `core::computeSlideId`, FNV-1a over file size and per-scene geometry. Keys the per-slide ICC profile store. Not cryptographic.
- **Annotation files are keyed by (slide ID, scene index)**, not the slide ID alone — `<slideId>.s<N>.annotations.json`. `computeSlideId` is file-level by construction, but annotations live in one scene's coordinate space, so keying on the ID alone would load a multi-scene file's scene 0 annotations against scene 1's coordinates.
- **`schemaVersion` does not move when a geometry type is added.** A higher version is refused outright, so bumping it would make every shipped build refuse rectangle-only files during a staggered rollout. An old build meeting a new shape fails on that shape alone, names it, and leaves the file intact.
- **A file we could not parse is never a file we write to.** A load failure or an unresolved slide-ID mismatch leaves `AnnotationPersistenceService` inactive, and an inactive service never writes. The serializer refuses anything the parser would reject, so the application cannot write a file it cannot read back.
- **`closeSlide()` flushes before it resets.** `endSlide()` is its first statement, ahead of `resetAnnotationState()`. Since `openScene()` and `reopenCurrentScene()` both route through `closeSlide()`, that one ordering is what makes annotations survive a slide switch and an ICC profile change. Do not move it into a `slideClosed` handler — that fires after the model is already empty.
- **Annotation geometry** uses `std::variant` over `RectangleGeometry` and `PolygonGeometry`, with free functions — `boundingBox`, `hitTest`, `typeOf`, `outline`, `translated` — dispatching by `std::visit`. One `PolygonGeometry` covers both the closed polygon and the open freehand line; they differ only in a `closed` flag. Adding a shape means adding an overload to each of those five, and nothing else. The `static_assert` guards that once forced this are gone — all five sites are now real dispatch.
- **`AnnotationType` is derived from the geometry**, never stored beside it. `Annotation::type()` delegates to `core::typeOf()`, and the annotation-level `"type"` field in a saved file is written from the geometry and ignored on read. The two cannot disagree.
- **The painted interior and the hit-tested interior must match.** `core::hitTest` uses even-odd ray casting; `paintAnnotations` sets `Qt::OddEvenFill`. A self-intersecting trace is valid data — pathologists produce them — and these two must agree about which parts of one are inside.
- **A pending multi-click shape is not an annotation.** It lives in `ViewportWidget::Impl::pending`, never reaches `AnnotationModel`, and is therefore never saved. It is discarded on slide close and on a tool change, and deliberately survives focus loss.
- **The drawing tool and the serializer must agree on what a valid shape is.** `ui::pendingShapeCommits` reads `core::kMinPolygonVertices` and `core::kMinPolylineVertices` — the same constants the serializer enforces on both read and write. A tool that could commit a shape the serializer would refuse would produce work that cannot be saved.
- **A double-click closing a multi-click shape keeps its vertex.** Qt delivers `MouseButtonDblClick` in place of the second press, not in addition to it, so the pair places exactly one vertex. `ViewportWidget::mouseDoubleClickEvent` must not remove one: on a triangle that would fall below the minimum and discard the shape silently.
- **Tile caching** uses LRU with a CPU budget and priority-based prefetching.
- **Colour management** via SlideIO's transformer, with a global default profile and per-slide overrides.

- **Annotation persistence** in JSON with schema versioning (`schemaVersion` 1; a higher version is refused outright rather than partially loaded).
- **Auto-save** debounced at 2s with a 30s backstop. The decision is the pure `core::shouldAutosave`, because no test suite creates a `QCoreApplication` and a `QTimer` never fires in a test — the timer is a dumb clock that asks it.

Planned, not built:

- **Undo/redo** via command pattern (50–200 levels per slide)
- **Advisory file locking** on annotation files (FR-ANN-32); today two instances editing one slide are last-writer-wins
- **Write-ahead log** and crash recovery (NFR-REL-04); today up to one autosave interval is lost on a hard kill
- **Plugin system** via shared libraries with versioned API, timeout monitoring, safe mode
