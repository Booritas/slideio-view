# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SlideIO Viewer is a cross-platform C++ desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It uses the **SlideIO library** for reading image data and **Qt 6** for the UI.

**Current state:** Implemented and building. The four layer libraries, the Qt application, a CMake/Conan build and four ctest suites all exist under `src/` and `tests/`. The viewer opens slides, navigates a tile pyramid on OpenGL, manages ICC colour profiles, and supports rectangle, polygon and freehand-line annotations (draw, select, move, delete) that **persist to disk** — one JSON file per (slide, scene) in a workspace folder under Documents, autosaved and reloaded when the slide reopens.

Annotation persistence is **in regular use and its documented checks are covered** as of 2026-10-13 — see item 0 of `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md`, which records what was verified and how. That item is now the list to re-run when this area changes, not a backlog.

**No test suite creates a `QApplication`**, so no widget can be constructed in a test and nothing in `ViewportWidget` is reachable from any suite. That is why a manual list exists at all. It also means a claim about runtime behaviour in this file is only as good as the last time somebody ran it: **do not infer from silence here that a path has never been exercised** — earlier drafts of this file and of item 0 did exactly that, asserted the subsystem had never been run, and were wrong. Ask the project owner, who tests this application regularly.

`documents/` holds the original design, much of which is still ahead of the code. Treat it as the plan, not a description of what is there; `documents/implementation-stages.md` tracks which stages have landed. Where a design document and the code disagree, the code is the truth — several design details turned out to be unimplementable as written.

**Not built yet:** vertex editing and the ellipse, circle, point, arrow and text shapes, the annotation layers panel, import/export, advisory file locking, cases, bookmarks, split view, remote slides, the plugin system, and Python scripting.

The annotation **list** and **properties** panels landed 2026-10-14 (sub-project F), together with the move of `AnnotationModel` out of `ViewportWidget` into `MainWindow`. Nineteen manual checks were run against that build — see the group V results in `docs/superpowers/specs/2026-10-14-annotation-panels-design.md`, which records what was observed rather than what was intended.

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
                          AnnotationSettings, Panels, dialogs                  (32 .cpp)
Application (Services) →  AnnotationModel, AnnotationPersistenceService,
                          UndoRedoStack, AnnotationEditCommand, AnnotationEditing,
                          WorkspaceSwitch, SlideViewerService, _CaseService_    (7 .cpp)
Domain (Core Logic)    →  Annotation, AnnotationSerialization, AutosavePolicy,
                          Iso8601, Viewport, TilePyramid, CoordinateSystem,
                          AnnotationMeasurement, AnnotationMerge,
                          WorkspaceSwitchPolicy, LevelRaster                   (20 .cpp)
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

**A running viewer silently turns `cmake --install` into a no-op.** Windows locks the running `.exe`, so the copy is skipped, the command still exits 0, and the installed binary stays at the previous build. Everything measured afterwards is the old code. Kill the viewer before installing. When a change has to be in the binary you are about to drive, confirm it is rather than assuming: compare the timestamps of `build/install/release/bin/slideio-viewer.exe` and `build/build/Release/slideio-viewer.exe`, or grep the installed binary for a string the change introduces. `ctest` after a failed build has the same shape — the suites run whatever binaries are already there and report green. **Both failures look exactly like success, so the exit code is not the thing to check; the artifact is.**

No test binary creates a `QApplication`, so **no test may construct a widget**. Logic that needs testing is extracted into pure functions over plain data; see `ColorProfilePolicy`, `AnnotationInteraction`, `DriverFilters`.

## Key Design Decisions

Shipped:

- **Content-derived slide IDs** — `core::computeSlideId`, FNV-1a over file size and per-scene geometry. Keys the per-slide ICC profile store. Not cryptographic.
- **Annotation files are keyed by (slide ID, scene index)**, not the slide ID alone — `<slideId>.s<N>.annotations.json`. `computeSlideId` is file-level by construction, but annotations live in one scene's coordinate space, so keying on the ID alone would load a multi-scene file's scene 0 annotations against scene 1's coordinates.
- **`schemaVersion` does not move when a geometry type is added.** A higher version is refused outright, so bumping it would make every shipped build refuse rectangle-only files during a staggered rollout. An old build meeting a new shape is designed to fail on that shape alone, name it, and leave the file intact; **observed 2026-10-12** (check P14) — a pre-branch build shown a polygon file refused it with `unknown geometry type 'polygon'` and left every byte unchanged.
- **A file we could not parse is never a file we write to.** A load failure or an unresolved slide-ID mismatch leaves `AnnotationPersistenceService` inactive, and an inactive service never writes. The serializer refuses anything the parser would reject, so the application cannot write a file it cannot read back.
- **`closeSlide()` flushes before it resets.** `endSlide()` is its first statement, ahead of `resetAnnotationState()`. Since `openScene()` and `reopenCurrentScene()` both route through `closeSlide()`, that one ordering is what makes annotations survive a slide switch and an ICC profile change. Do not move it into a `slideClosed` handler — that fires after the model is already empty.
- **Annotation geometry** uses `std::variant` over `RectangleGeometry` and `PolygonGeometry`, with free functions — `boundingBox`, `hitTest`, `typeOf`, `outline`, `translated` — dispatching by `std::visit`. One `PolygonGeometry` covers both the closed polygon and the open freehand line; they differ only in a `closed` flag. Adding a shape means adding an overload to each of those five, plus one to every other exhaustive `std::visit` over the variant — the serializer's writer side among them. None of these can be forgotten silently: each missing overload is a compile error naming the site. **One site is the exception:** `readGeometry` in `AnnotationSerialization.cpp` is a string-comparison chain with no compile-time link to the variant, so it must be remembered by hand. Forget its branch and the application writes files it then refuses to open, breaking the "never write a file we cannot read back" invariant. A new geometry type must therefore land with a serializer round-trip test, because nothing else forces its reader branch to exist. The `static_assert` guards that once forced this are gone — the five sites they guarded are now real dispatch.
- **A selection always names an annotation the model holds.** `AnnotationModel::setSelected` refuses an id it cannot find, the empty id excepted — that is how a selection is cleared. A selection naming nothing paints no handles and matches no delete, and the model is the only place that can tell.
- **`AnnotationType` is derived from the geometry**, never stored beside it. `Annotation::type()` delegates to `core::typeOf()`, and the annotation-level `"type"` field in a saved file is written from the geometry and ignored on read. The two cannot disagree.
- **The painted interior and the hit-tested interior must match.** `core::hitTest` uses even-odd ray casting; `paintAnnotations` sets `Qt::OddEvenFill`. A self-intersecting trace is valid data — pathologists produce them — and these two must agree about which parts of one are inside.
- **A pending multi-click shape is not an annotation.** It lives in `ViewportWidget::Impl::pending`, never reaches `AnnotationModel`, and is therefore never saved. It is discarded on slide close and on a tool change, and survives focus loss — **observed 2026-10-12** (check P5): another window stole focus mid-trace and the preview came back identical.
- **The drawing tool and the serializer must agree on what a valid shape is.** `ui::pendingShapeCommits` reads `core::kMinPolygonVertices` and `core::kMinPolylineVertices` — the same constants the serializer enforces on both read and write. A tool that could commit a shape the serializer would refuse would produce work that cannot be saved.
- **A double-click closing a multi-click shape keeps its vertex.** Qt delivers `MouseButtonDblClick` in place of the second press, not in addition to it, so the pair places exactly one vertex. `ViewportWidget::mouseDoubleClickEvent` must not remove one: on a triangle that would fall below the minimum and discard the shape silently. The same rule holds for any close gesture: one that cannot reach the vertex minimum does not close and does not discard — it leaves the trace alive, and `commitPendingShape` makes that test before it destroys anything.
- **Tile caching** uses LRU with a CPU budget and priority-based prefetching.
- **Colour management** via SlideIO's transformer, with a global default profile and per-slide overrides.

- **Annotation persistence** in JSON with schema versioning (`schemaVersion` 1; a higher version is refused outright rather than partially loaded).
- **Auto-save** debounced at 2s with a 30s backstop. The decision is the pure `core::shouldAutosave`, because no test suite creates a `QCoreApplication` and a `QTimer` never fires in a test — the timer is a dumb clock that asks it.
- **Undo/redo** per slide via the command pattern, 50 steps
  (`app::UndoRedoStack`, `app::AnnotationEditCommand`). One command type covers
  create, delete and move, because the inverse is the same shape in all three:
  the state before and the state after. Commands are recorded *after* the model
  has already been changed through its own API, so `execute()` runs only on
  redo. The stack is cleared when the model is emptied for a different slide or
  scene — but not when the same scene is reopened for a colour-profile change,
  where the annotations come back under the same ids. Ctrl+Z during a live
  multi-click trace removes the last vertex instead, which is why the Undo
  action must stay enabled while tracing: a disabled `QAction` is removed from
  Qt's shortcut map and its key never fires. A menu action's shortcut is
  window-scoped and Qt matches it *before* the focus widget sees the key, so
  the Edit actions hand Ctrl+Z and Ctrl+Y to a focused text editor first —
  without that, typing in a dock's filter box and pressing Ctrl+Z would undo an
  annotation edit and autosave would write it.

Planned, not built:

- **Advisory file locking** on annotation files (FR-ANN-32); today two instances editing one slide are last-writer-wins
- **Write-ahead log** and crash recovery (NFR-REL-04); today up to one autosave interval is lost on a hard kill
- **Plugin system** via shared libraries with versioned API, timeout monitoring, safe mode
