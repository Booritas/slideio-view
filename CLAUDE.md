# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SlideIO Viewer is a cross-platform C++ desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It uses the **SlideIO library** for reading image data and **Qt 6** for the UI.

**Current state:** Design and specification phase only. The `documents/` directory contains the complete design — no source code, build system, or tests exist yet.

## Design Documents

All specifications live under `documents/`. Read them in this order for context:

1. **`01-software-requirements-specification.md`** — Functional/non-functional requirements, user personas, clinical workflows
2. **`02-user-interface-design.md`** — UI layout, interaction patterns, keyboard shortcuts, annotation workflows
3. **`03-software-architecture-and-design.md`** — Layered architecture, component design, data flows, class structures, threading model
4. **`phase4-implementation-strategy.md`** — C++ coding standards, project structure, CMake targets, example class definitions
5. **`phase5-critical-review.md`** — Known design issues, performance risks, and recommended mitigations

Supporting discovery documents (`phase1-requirements-discovery.md`, `phase2-ux-concept.md`, `phase3-system-architecture.md`) contain the iterative design discussion. `claude-team.md` is the multi-agent prompt that generated the design.

## Planned Architecture

**Layered, strict downward dependency:**

```
Presentation (Qt UI)  →  MainWindow, ViewportWidget, Panels, Toolbars
Application (Services) →  AnnotationService, SlideViewerService, CaseService
Domain (Core Logic)    →  Annotation, Viewport, TilePyramid, CoordinateSystem
Infrastructure         →  SlideIOAdapter, LruTileCache, RemoteClient, AnnotationStore
```

- Domain layer is **pure C++17, Qt-free** — enforced via separate CMake static library targets
- Infrastructure implements domain interfaces (`ISlideSource`, `ITileCache`, `IAnnotationRepository`)
- SlideIO access is isolated behind `SlideIOAdapter` / `SlideIOAdapterPool`

## Planned Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++17 (guarded C++20 features) |
| UI | Qt 6 Widgets |
| Rendering | OpenGL 3.3+ with instanced tile rendering |
| Slide I/O | SlideIO library via adapter pool |
| Build | CMake 3.20+, Conan |
| JSON | nlohmann_json |
| Logging | spdlog |
| Compilers | MSVC 2022, GCC 12+, AppleClang 14+ |

## Planned Coding Conventions

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

## Planned CMake Targets

| Target | Links Against |
|--------|--------------|
| `slideio-viewer-core` | None (pure C++) |
| `slideio-viewer-app` | core, Qt6::Core |
| `slideio-viewer-infra` | core, app, Qt6::Core, Qt6::Network, SlideIO, nlohmann_json, spdlog |
| `slideio-viewer-ui` | core, app, infra, Qt6::Widgets, Qt6::OpenGLWidgets |
| `slideio-viewer` | ui (transitively all) |

## Key Design Decisions

- **Annotation geometry** uses `std::variant` (Polygon, Ellipse, LineString, Point, Arrow, Angle) with free functions for hit-test, bounding box, area
- **Tile caching** uses LRU with separate CPU and GPU memory budgets, priority-based prefetching
- **Undo/redo** via command pattern (50-200 levels per slide)
- **Annotation persistence** in JSON with schema versioning; content-derived slide IDs
- **Auto-save** debounced at 2s with 30s periodic backup
- **Plugin system** via shared libraries with versioned API, timeout monitoring, safe mode
