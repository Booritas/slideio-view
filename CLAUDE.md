# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SlideIO Viewer is a cross-platform C++ desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It uses the **SlideIO library** for reading image data and **Qt 6** for the UI.

**Current state:** Implemented and building. The four layer libraries, the Qt application, a CMake/Conan build and four ctest suites all exist under `src/` and `tests/`. The viewer opens slides, navigates a tile pyramid on OpenGL, manages ICC colour profiles, and supports rectangle, polygon and freehand-line annotations (draw, select, move, delete) that **persist to disk** — one JSON file per (slide, scene) in a workspace folder under Documents, autosaved and reloaded when the slide reopens.

Annotation persistence is **in regular use and its documented checks are covered** as of 2026-10-13 — see item 0 of `docs/superpowers/specs/2026-10-11-annotation-persistence-followups.md`, which records what was verified and how. That item is now the list to re-run when this area changes, not a backlog.

**No test suite creates a `QApplication`**, so no widget can be constructed in a test and nothing in `ViewportWidget` is reachable from any suite. That is why a manual list exists at all. It also means a claim about runtime behaviour in this file is only as good as the last time somebody ran it: **do not infer from silence here that a path has never been exercised** — earlier drafts of this file and of item 0 did exactly that, asserted the subsystem had never been run, and were wrong. Ask the project owner, who tests this application regularly.

`documents/` holds the original design, much of which is still ahead of the code. Treat it as the plan, not a description of what is there; `documents/implementation-stages.md` tracks which stages have landed. Where a design document and the code disagree, the code is the truth — several design details turned out to be unimplementable as written.

**Not built yet:** the text shape, the annotation layers panel, import/export, advisory file locking, cases, bookmarks, split view, remote slides, the plugin system, and Python scripting.

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
- **Annotation geometry** uses `std::variant` over `RectangleGeometry`, `PolygonGeometry` and `EllipseGeometry`, with free functions — `boundingBox`, `hitTest`, `typeOf`, `outline`, `translated`, `hasWellDefinedArea` — dispatching by `std::visit`. One `PolygonGeometry` covers both the closed polygon and the open freehand line; they differ only in a `closed` flag. `PointGeometry` and `ArrowGeometry` are separate alternatives rather than degenerate cases of those: a marker is not a zero-extent rectangle, and an arrow cannot be a two-vertex polyline because `typeOf()` derives the type from the geometry and the two would be indistinguishable. **An arrow's endpoint pair is ORDERED** — `tail` then `head` — and must never be normalised the way a rectangle's corners are, or every arrow drawn right-to-left silently reverses. Adding a shape means adding an overload to each of those six, plus one to every other exhaustive `std::visit` over the variant. **Adding the ellipse in 2026-10 fired ten such sites**, of which the design spec had enumerated five: the six in `AnnotationGeometry.cpp`, then `shapeToJson`, `isRepresentableShape` and `geometryTypeName`'s `static_assert` in `AnnotationSerialization.cpp`, and `ShapeNameVisitor` in `src/ui/src/AnnotationShapeName.cpp` — which also has a `switch` over `AnnotationType` beside it. **Adding the point and arrow a day later fired twelve**: those eleven — counting `ShapeNameVisitor` and the `AnnotationType` switch beside it separately — plus `replaceBox` in `BoxResize.cpp`, which box resizing had added in between. That twelfth is the first demonstration that `withBoxReplaced`'s `std::visit` earns what it costs: both new types were forced to answer `std::nullopt` where a `get_if` chain would have let them fall silently through to "not resizable". **The next shape will fire thirteen** — `decorationFor` in `AnnotationDecoration.cpp` is a new one, deliberately a visited overload set rather than a `get_if` chain for the same reason. Do not trust a count; let the compiler enumerate them. None of these can be forgotten silently: each missing overload is a compile error naming the site. **One site is the exception:** `readGeometry` in `AnnotationSerialization.cpp` is a string-comparison chain with no compile-time link to the variant, so it must be remembered by hand. Forget its branch and the application writes files it then refuses to open, breaking the "never write a file we cannot read back" invariant. A new geometry type must therefore land with a serializer round-trip test, because nothing else forces its reader branch to exist. The `static_assert` guards that once forced the five geometry-dispatch sites are gone — those are real overload dispatch now. **One `static_assert` remains**, in `geometryTypeName` (`AnnotationSerialization.cpp:206`), and it is one of the twelve sites above.
- **An ellipse is its inscribed 512-gon.** `EllipseGeometry` stores a bounding box; `outline()` expands it into `kEllipseOutlineVertices` points, and the painter, `hitTest` and `measure` all read that one outline, so they cannot disagree. The count is fixed and never adaptive: an adaptive one would make a stored annotation's measured area depend on the zoom it was measured at. The area therefore reads about 0.0025% under the true ellipse — deterministic, documented, and far inside the scanner's own calibration uncertainty. `core::measure()` short-circuits `isSelfIntersecting` for any shape `hasWellDefinedArea` accepts: at 512 vertices that test costs ~131k segment-pair comparisons, and on a *degenerate* ellipse its collinear-overlap rule would otherwise withhold an area that is plainly zero — so skipping it is more correct, not merely cheaper. **That predicate is named for what it promises.** It was `isConvexByConstruction`, then nearly `cannotSelfIntersect`; both were false for a degenerate ellipse or zero-extent rectangle, which are collinear and whose generated vertices do overlap. Only the area claim survives those inputs. Do not reach for it to justify a convex-hull or point-in-convex-polygon shortcut. See `docs/superpowers/specs/2026-10-16-annotation-ellipse-design.md` §3.

- **Vertex editing is a mode, and it addresses STORED vertices.** Double-clicking a committed polygon or freehand line enters it; Escape, a press off the shape, a tool change or a slide close leaves it. While it is on, `ViewportWidget` paints a handle per vertex and routes presses through `ui::resolveVertexEditPress` — except under held space, which still pans, because that promise predates the mode. Every operation — `core::nearestVertex`, `nearestEdge`, `withVertexMoved`, `withVertexInserted`, `withVertexRemoved` — indexes `poly.vertices` and **never** `core::outline()`, which drops consecutive duplicates and so does not address stored vertices; every polygon saved before 2026-10-17 carries such a duplicate. These take `PolygonGeometry` directly rather than the variant, which is why adding them fired none of the ten dispatch sites. Removal refuses below the serializer's own minimum, so the tool cannot build a shape the format would reject. One completed gesture is one undo step, and `core::sameVertices` — not `ui::moveEdit`, which compares bounding boxes and is blind to an interior vertex moving inside an unchanged extent — decides whether anything changed. See `docs/superpowers/specs/2026-10-17-annotation-vertex-editing-design.md`.

- **Box resize handles are live on selection, with no mode.** A selected `RectangleGeometry` or `EllipseGeometry` paints eight handles — four corners, four edge midpoints — and a press on one starts a resize, tested *before* `resolveDragOwner` because a corner handle sits on the outline and would otherwise be taken as a move. No mode, deliberately: the vertex-editing mode exists only because 400 handles would bury a trace, and eight cannot. The core operations take a plain `RectF`, so the same three functions resize a rectangle and an ellipse; only `core::withBoxReplaced` touches the variant, and it uses `std::visit` rather than `get_if` so a future box-shaped type is a compile error rather than a silent "not resizable". Dragging a handle past its opposite edge flips the box, as a creation drag may, and `withMinimumExtent` widens a collapsed axis at commit. **Six exits record the undo step** — release, focus loss, Escape, undo mid-drag, tool change and slide close — all through `recordBoxResize()`; vertex editing found that list one review at a time and this one was written from it. Note two of the six are traps the surrounding code sets: `endToolDrag()` returns early unless `dragOwner == Tool`, and `resetAnnotationState()` must clear the new members or an annotation from one slide is restored onto the next. Twenty-three manual checks were run against this build — group AA, 23/23, recorded in `docs/superpowers/specs/2026-10-17-annotation-box-resize-design.md` §7.3 along with how a read-only slide is produced for a manual check.

- **A marker and an arrowhead are sized in screen pixels, by core.** `core::screenDecoration` (`AnnotationDecoration.{h,cpp}`) answers, for one geometry, what glyph it needs and how many pixels across — a `Disc` whose size is a radius, an `ArrowHead` whose size is its length along the shaft, or nothing at all. The painter is told a kind, an anchor in slide units and a pixel size, and still has no idea what shape it is drawing; it derives the head's direction from the two shaft points it has already converted, which is correct because the viewport only pans and zooms. **The disc radius is a parameter, not a core constant:** `kHitToleranceScreenPixels` lives in ui and core cannot reach it, so `ViewportWidget` passes the same value it passes to the hit test and the marker you see is the target you click by construction. The arrowhead's two constants go the other way and live in core, because the arithmetic that reads them is there — the head is the smaller of `kArrowHeadScreenPixels` and `kArrowHeadMaxShaftFraction` of the shaft, since fixed alone outgrows a short arrow and proportional alone disappears. The rejected alternative was making `outline()` take a scale, which the ellipse spec forbids: a stored annotation's measured value would then depend on the zoom it happened to be measured at. A selected marker is **ringed** rather than outlined, because a point's outline is one vertex and the ordinary selection stroke paints nothing at all. See `docs/superpowers/specs/2026-10-17-annotation-point-arrow-design.md`.

- **Every path that creates an annotation asks `annotationsEditable()` first.** `commitPendingShape` has since persistence landed; `commitDrawDrag` did not until 2026-10-17, so the rectangle and ellipse tools were creating a shape on a read-only slide in silence. It is hard to reach — a read-only slide disables the drawing tool *actions* — but the active tool is deliberately not reset when a slide opens, so picking a tool on an editable slide and then opening a read-only one leaves it live. The refusal is noisy (`annotationEditBlocked`) where a misclick is silent: a gesture too small to be a shape is the user's own doing, and a gesture refused by the slide is not.

- **A selection always names an annotation the model holds.** `AnnotationModel::setSelected` refuses an id it cannot find, the empty id excepted — that is how a selection is cleared. A selection naming nothing paints no handles and matches no delete, and the model is the only place that can tell.
- **`AnnotationType` is derived from the geometry**, never stored beside it. `Annotation::type()` delegates to `core::typeOf()`, and the annotation-level `"type"` field in a saved file is written from the geometry and ignored on read. The two cannot disagree.
- **The painted interior and the hit-tested interior must match.** `core::hitTest` uses even-odd ray casting; `paintAnnotations` sets `Qt::OddEvenFill`. A self-intersecting trace is valid data — pathologists produce them — and these two must agree about which parts of one are inside.
- **A pending multi-click shape is not an annotation.** It lives in `ViewportWidget::Impl::pending`, never reaches `AnnotationModel`, and is therefore never saved. It is discarded on slide close and on a tool change, and survives focus loss — **observed 2026-10-12** (check P5): another window stole focus mid-trace and the preview came back identical.
- **The drawing tool and the serializer must agree on what a valid shape is.** `ui::pendingShapeCommits` reads `core::kMinPolygonVertices` and `core::kMinPolylineVertices` — the same constants the serializer enforces on both read and write. A tool that could commit a shape the serializer would refuse would produce work that cannot be saved.
- **A double-click closing a multi-click shape keeps its vertex.** Qt delivers `MouseButtonDblClick` in place of the second press, not in addition to it, so the pair places exactly one vertex. `ViewportWidget::mouseDoubleClickEvent` must not remove one: on a triangle that would fall below the minimum and discard the shape silently. The same rule holds for any close gesture: one that cannot reach the vertex minimum does not close and does not discard — it leaves the trace alive, and `commitPendingShape` makes that test before it destroys anything. **But the vertex it keeps must not duplicate the previous one.** The cursor has usually not moved since the last trailed point — the user stopped to double-click — so the click placed a vertex exactly on top of it. That zero-length segment paints nothing and measures nothing, but it makes the segments on either side share an endpoint, and `core::isSelfIntersecting` only excludes segments adjacent *by index*. The finished polygon then read as self-intersecting and its **area was withheld**, on roughly half of all hand-traced shapes, until 2026-10-17. Two guards now: `ui::shouldPlaceClickVertex` refuses the coincident click, and `core::outline()` drops consecutive duplicates so annotations already on disk measure correctly without being rewritten.
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
