# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SlideIO Viewer is a cross-platform C++ desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It uses the **SlideIO library** for reading image data and **Qt 6** for the UI.

**Current state:** Implemented and building. The four layer libraries, the Qt application, a CMake/Conan build and four ctest suites all exist under `src/` and `tests/`. The viewer opens slides, navigates a tile pyramid on OpenGL, manages ICC colour profiles, and has the first slice of annotation support (rectangles: draw, select, move, delete — in memory only).

`documents/` holds the original design, much of which is still ahead of the code. Treat it as the plan, not a description of what is there; `documents/implementation-stages.md` tracks which stages have landed. Where a design document and the code disagree, the code is the truth — several design details turned out to be unimplementable as written.

**Not built yet:** annotation persistence, undo/redo, cases, bookmarks, split view, remote slides, the plugin system, and Python scripting.

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
Presentation (Qt UI)   →  MainWindow, ViewportWidget, Panels, dialogs          (23 .cpp)
Application (Services) →  AnnotationModel, SlideViewerService, _CaseService_    (2 .cpp)
Domain (Core Logic)    →  Annotation, Viewport, TilePyramid, CoordinateSystem  (13 .cpp)
Infrastructure         →  SlideIOAdapter, LruTileCache, TileLoadScheduler,
                          QSettingsColorProfileOverrideStore, _RemoteClient_    (5 .cpp)
```

- Core is **pure C++17, Qt-free** — enforced by it linking no Qt target at all. Anything needing `QUuid`, `QSettings` or signals belongs one layer up.
- Infrastructure implements core interfaces (`ISlideSource`, `ITileCache`). `IAnnotationRepository` is not written yet.
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

JSON is **not** a dependency yet — nlohmann_json appears only as a credit string in the About dialog, and nothing links it. It arrives with annotation persistence. Qt6::Network is likewise unused.

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

- **Content-derived slide IDs** — `core::computeSlideId`, FNV-1a over file size and per-scene geometry. Keys the per-slide ICC profile store and will key annotation files. Not cryptographic.
- **Annotation geometry** uses `std::variant`, currently with one alternative (Rectangle) and free functions for bounding box and hit-test. Two sites rebuild a shape from its bounding box and `static_assert` the variant's size, so adding a type fails to compile rather than silently converting.
- **Tile caching** uses LRU with a CPU budget and priority-based prefetching.
- **Colour management** via SlideIO's transformer, with a global default profile and per-slide overrides.

Planned, not built:

- **Undo/redo** via command pattern (50–200 levels per slide)
- **Annotation persistence** in JSON with schema versioning
- **Auto-save** debounced at 2s with 30s periodic backup
- **Plugin system** via shared libraries with versioned API, timeout monitoring, safe mode
