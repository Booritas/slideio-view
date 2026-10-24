# Annotation Layers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Per-scene annotation layers (Diagnostic, Teaching, Research by default) with visibility, lock, opacity and order, a Layers tab, Move to Layer, and a file format an older build refuses rather than flattens.

**Architecture:** A core `AnnotationLayer` model with pure rules (resolution, paint order, version decision, validation) and the serializer's conditional version 2; `AnnotationModel` owns a scene's layers, the active layer and the layer-move command; pure ui policies for the panel, the list and the tally; then the Layers tab and the viewport's hidden/locked/opacity/z-order behaviour.

**Tech Stack:** C++17, Qt 6.7 Widgets, Catch2 v3, CMake.

**Spec:** `docs/superpowers/specs/2026-10-24-annotation-layers-design.md`

## Global Constraints

- Default layers, bottom to top: `{"diagnostic","Diagnostic"}`, `{"teaching","Teaching"}`, `{"research","Research"}`, all `visible = true`, `locked = false`, `opacity = 1.0`.
- `layers[0]` is the bottom; order is z-order (higher index paints on top, hit first).
- An annotation's layer is always `core::effectiveLayerId(annotation, layers)`: its `layerId` when the scene has it, else `layers[0].id`.
- Written `schemaVersion` is **1** with no `layers` key and no `layerId` keys unless `core::layersCarryInformation` is true, then **2** with both. `kCurrentSchemaVersion` = 2 (highest read). A version-1 file reads as the defaults.
- Valid layer list: non-empty, unique non-empty ids, non-empty names, finite opacity in [0, 1].
- A scene keeps at least one layer; only an empty layer can be deleted.
- Counts (tally, counts CSV, measurements CSV) include every layer; the list and the slide omit hidden layers.
- Locked layer: no move, resize, vertex edit, delete, property edit, Move to Layer; drawing/importing into a hidden or locked active layer is refused with "Layer <name> is hidden" / "Layer <name> is locked".
- Layer assignment changes are undo steps (many-annotation command via `restoreMany`, never `modelReset`); layer structure edits are not.
- Non-ASCII literals as byte escapes; core stays Qt-free.

## Review Focus

1. **A file whose only layer information is a hidden or renamed default layer** — must still be written as version 2 (else an older build silently drops the hidden state). Pinned in Task 1 (`layersCarryInformation` true for each single deviation).
2. **Undo of a Move to Layer after the target or source layer was deleted** — the annotation lands on the bottom layer, never on no layer, and the list/tally do not crash or drop it. Pinned in Task 2 (resolution after deletion) and Task 3 (tally with a dangling layerId).
3. **The selected annotation's layer is hidden or locked** — selection clears on hide; on lock, Delete and drag are refused; vertex editing / resize / text editing on it end. Owned by Task 4; checked AM3/AM4.
4. **A read-only scene** — visibility toggles work and are not saved; every other layer control is disabled, and nothing in the layer UI marks the scene dirty. Owned by Task 4; checked AM10.
5. **A version-2 file with an annotation whose layerId is unknown, or a layers list with a duplicate id** — the first loads (annotation on bottom layer), the second is refused naming the problem and never rewritten. Pinned in Task 1.

---

### Task 1: Layers in core and the file

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationLayer.h`, `src/core/src/AnnotationLayer.cpp`
- Modify: `src/core/include/slideio/viewer/core/Annotation.h`, `src/core/src/Annotation.cpp` (`layerId`), `src/core/include/slideio/viewer/core/AnnotationDocument.h` (`layers`, `kCurrentSchemaVersion = 2`), `src/core/src/AnnotationSerialization.cpp`, `src/core/include/slideio/viewer/core/AnnotationGeoJson.h` + `.cpp` (layer name in `slideio`), `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/AnnotationLayerTest.cpp`, `tests/core/AnnotationSerializationTest.cpp`, `tests/core/AnnotationGeoJsonTest.cpp`

**Interfaces:**
- Produces (namespace `slideio::viewer::core`):
  - `struct AnnotationLayer { std::string id; std::string name; bool visible = true; bool locked = false; double opacity = 1.0; };` with `operator==`.
  - `[[nodiscard]] std::vector<AnnotationLayer> defaultLayers();`
  - `[[nodiscard]] std::string effectiveLayerId(const Annotation&, const std::vector<AnnotationLayer>& layers);`
  - `[[nodiscard]] const AnnotationLayer* findLayer(const std::vector<AnnotationLayer>&, const std::string& id);`
  - `[[nodiscard]] bool layersCarryInformation(const std::vector<AnnotationLayer>& layers, const std::vector<Annotation>& annotations);`
  - `[[nodiscard]] bool isValidLayerList(const std::vector<AnnotationLayer>& layers, std::string& problem);`
  - `[[nodiscard]] std::vector<std::size_t> paintOrder(const std::vector<Annotation>& annotations, const std::vector<AnnotationLayer>& layers);` — indices, bottom layer first, model order within a layer.
  - `Annotation::layerId()` / `setLayerId(std::string)` (default empty).
  - `AnnotationDocument::layers` (empty means "the file said nothing" → caller uses defaults).
  - `GeoJsonExportContext::layerNames` (`std::map<std::string, std::string>` id → name); each feature's `slideio.layer` is the name of `effectiveLayerId` (given layers via a new `GeoJsonExportContext::layers` vector — use that instead of a map if simpler; decide and ledger).

- [ ] **Step 1: Write the failing tests:**
  - layer rules: `defaultLayers()` ids/names/order; `effectiveLayerId` for "" → "diagnostic", for "teaching" → "teaching", for "gone" → bottom id; `layersCarryInformation` false for defaults + annotations with empty or `diagnostic` layerId, and **true for each single deviation**: an annotation on `teaching`; Teaching hidden; Research locked; Diagnostic opacity 0.5; Teaching renamed; layers reordered; a fourth layer; a default deleted; `isValidLayerList` refuses empty list, duplicate id, empty id, empty name, opacity 1.5 / NaN; `paintOrder` with layers [d, t] and annotations [t0, d1, t2, d3] → [1, 3, 0, 2].
  - serialization: a document with default layers and all-Diagnostic annotations serialises **byte-identical** to the same document before this change (compare with the existing `makeDocument()` round-trip output asserted against a stored expected string, or assert `schemaVersion: 1` and absence of `"layers"` and `"layerId"`); one with an annotation on Teaching writes `schemaVersion` 2, `"layers"` in order and `"layerId"` on every annotation; a v2 round trip preserves every layer field and assignment; a v1 file parses with `document.layers` empty; a v2 file with a duplicate layer id is `InvalidField` naming it; a v2 annotation with an unknown `layerId` loads (kept as written); a `schemaVersion` 3 file is still refused.
  - GeoJSON: a feature's `slideio.layer` is the layer name ("Teaching"), and "Diagnostic" for an annotation with an empty layerId.
- [ ] **Step 2: Run** `cmake --build build/build --config Release --target slideio-viewer-core-tests` — Expected: compile error, `AnnotationLayer.h` missing.
- [ ] **Step 3: Implement** per the spec §3–4; the serializer decides the written version with `layersCarryInformation(document.layers.empty() ? defaultLayers() : document.layers, document.annotations)`.
- [ ] **Step 4: Run** `build/build/tests/Release/slideio-viewer-core-tests.exe` and `ctest --test-dir build/build -C Release` — Expected: all pass, 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Add annotation layers to core and the file format"`

### Task 2: Layers in the model, undo and persistence

**Files:**
- Modify: `src/app/include/slideio/viewer/app/AnnotationModel.h`, `src/app/src/AnnotationModel.cpp`, `src/app/src/AnnotationPersistenceService.cpp`
- Create: `src/app/include/slideio/viewer/app/AnnotationLayerMoveCommand.h`, `src/app/src/AnnotationLayerMoveCommand.cpp` (list explicitly in `src/app/CMakeLists.txt`, as its other headers are)
- Test: `tests/app/AnnotationModelTest.cpp`, `tests/app/AnnotationLayerMoveCommandTest.cpp`, `tests/app/AnnotationPersistenceServiceTest.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces (`app::AnnotationModel`): `const std::vector<core::AnnotationLayer>& layers() const;` `void setLayers(std::vector<core::AnnotationLayer>)` (validated; refuses an invalid list; keeps the active layer if it still exists, else the bottom); `const std::string& activeLayerId() const;` `void setActiveLayer(const std::string&)`; `std::vector<std::string> moveToLayer(const std::vector<std::string>& ids, const std::string& layerId)` (returns the ids actually moved; restores through `restoreMany`, so one `annotationsBulkChanged`); signal `layersChanged()`; `add()` stamps `activeLayerId()`; `hitTest` skips annotations on hidden layers and searches in reverse `paintOrder`; `clear()`/`replaceAll()` reset layers to `defaultLayers()` and the active layer to `diagnostic`; `void replaceAll(std::vector<core::Annotation>, std::vector<core::AnnotationLayer> layers)` overload for the load path (empty → defaults).
  - `app::AnnotationLayerMoveCommand::create(model, before, after, description)` — before/after are the whole annotations, as the retag command's are.
  - Persistence: loads `document.layers` into the model; saves `m_model.layers()` into the document; `layersChanged` marks dirty (only while active).

- [ ] **Step 1: Write the failing tests:** `add` stamps the active layer; `setActiveLayer` to an unknown id is ignored; `setLayers` with a duplicate id is refused and emits nothing; `moveToLayer` moves, emits one bulk change, and its command undoes and redoes as one step; undo of a move after the source layer was removed from `setLayers` leaves the annotation resolving to the bottom layer (Review Focus 2); `hitTest` ignores an annotation on a hidden layer and picks the higher layer where two overlap regardless of model order; `clear()` restores the defaults; persistence: a loaded v2 document's layers reach the model, a `setLayers` while active marks dirty and the saved document carries them.
- [ ] **Step 2: Run** the app-tests build — Expected: compile errors on the new API.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release --output-on-failure` — Expected: 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Hold layers in the annotation model with undoable moves"`

### Task 3: The ui rules — tally, list, panel

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/LayersPanelPolicy.h`, `src/ui/src/LayersPanelPolicy.cpp`
- Modify: `src/ui/src/AnnotationListPolicy.cpp` (+ header), `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/ui/LayersPanelPolicyTest.cpp`, `tests/ui/AnnotationListPolicyTest.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces (namespace `ui`):
  - `[[nodiscard]] std::size_t hiddenLayerCount(const std::vector<core::Annotation>&, const std::vector<core::AnnotationLayer>&);` — annotations the tally counts that sit on hidden layers (texts, rulers and angles excluded, as the tally excludes them).
  - `annotationRows(...)` gains a `const std::vector<core::AnnotationLayer>& layers` parameter (default: none = no filtering) and omits rows on hidden layers; `AnnotationRow` gains `bool locked`.
  - `struct LayerRow { std::string id; std::string name; std::size_t count; bool visible; bool locked; double opacity; bool active; };` `[[nodiscard]] std::vector<LayerRow> layerRows(layers, annotations, activeId);` — **top layer first**, counts via `effectiveLayerId`.
  - `[[nodiscard]] bool canDeleteLayer(const std::vector<core::AnnotationLayer>&, const std::vector<core::Annotation>&, const std::string& id);` — empty and not the last.
  - `[[nodiscard]] std::vector<std::string> mergeTargets(layers, sourceId);` — every other layer id, top first.
  - `[[nodiscard]] bool canMoveLayerUp(layers, id)` / `canMoveLayerDown(layers, id)` — "up" is towards the top (higher index).
  - `[[nodiscard]] std::vector<core::AnnotationLayer> withLayerMoved(layers, id, int delta);`
  - `[[nodiscard]] std::string layerRefusal(const core::AnnotationLayer& layer);` — `"Layer <name> is hidden"` / `"Layer <name> is locked"` / `""`.

- [ ] **Step 1: Write the failing tests** for each function above, including: `hiddenLayerCount` 2 for two markers on a hidden layer and a hidden-layer ruler ignored; the tally itself still counts hidden-layer markers (unchanged behaviour, now pinned); a row whose annotation has a dangling layerId is shown under the bottom layer (Review Focus 2); `canDeleteLayer` false for a non-empty layer and for the only layer; `layerRows` top-first with counts and the active flag.
- [ ] **Step 2: Run** the ui-tests build — Expected: compile errors.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release --output-on-failure` — Expected: 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Add the layer rules for the panel, the list and the tally"`

### Task 4: The Layers tab and the viewport

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/LayersPanel.h`, `src/ui/src/LayersPanel.cpp`
- Modify: `src/ui/src/MainWindow.cpp`, `src/ui/src/ViewportWidget.cpp` (+ header), `src/ui/src/AnnotationListPanel.cpp`, `src/ui/src/AnnotationPropertiesPanel.cpp`, `src/ui/src/StatusBarManager.cpp` (+ header), `CLAUDE.md`

**Interfaces:**
- Consumes: Tasks 1–3.

- [ ] **Step 1: LayersPanel** (QDockWidget tabified with the other right-hand docks, title "Layers"): rows from `layerRows` (eye and padlock check boxes, name, count, opacity slider 0–100), the active row bold; click a row → `setActiveLayer`; double-click → rename in place; buttons New ("Layer <n>"), Delete (`canDeleteLayer`), Merge into… (menu from `mergeTargets`; one undo step "Merge <A> into <B>" via `moveToLayer` + `AnnotationLayerMoveCommand`), Up/Down (`withLayerMoved`). Structure edits call `model.setLayers`. A `setEditingAllowed(bool)` disables everything but the eye boxes on a read-only scene. Rebuild on `layersChanged`, `annotationAdded/Removed`, `annotationsBulkChanged`, `modelReset`, `cleared`.
- [ ] **Step 2: MainWindow** — create and tabify the panel; `applyAnnotationAvailability` calls `setEditingAllowed`; a status-bar label "Layer: <name>" updated on `layersChanged` and cleared on close; **Move to Layer** submenu in the list panel's and the viewport's context menus (targets: every other layer; refused with `layerRefusal` when the source or target is locked); visibility toggles on a read-only scene go through `setLayers` but the persistence service is inactive there, so nothing is written — confirm it.
- [ ] **Step 3: Viewport** — paint in `core::paintOrder`, skipping hidden layers, multiplying stroke/fill/plate/label alpha by the layer's opacity; hit tests through the model's `hitTest` and the text-plate search in the same reverse paint order, skipping hidden layers; one `bool annotationLocked(const std::string& id) const` consulted by move, resize, vertex editing, Delete, the text editor, and `ViewportWidget`'s edit entry points, refusing with `annotationEditBlocked(layerRefusal(...))`; drawing tools and the Text tool refuse when the active layer is hidden or locked; on `layersChanged`, clear the selection if its layer is now hidden and end vertex editing / resize / text editing on a hidden or locked annotation.
- [ ] **Step 4: List and properties panels, strip** — the list passes the layers to `annotationRows`, marks locked rows (padlock glyph or "(locked)" in the Type column); the properties panel disables its fields for an annotation on a locked layer; the counting strip shows "Includes <n> on hidden layers" from `hiddenLayerCount` when n > 0.
- [ ] **Step 5: Imports** — `MainWindow`'s import commit refuses with `layerRefusal` when the active layer is hidden or locked, and the imported annotations take the active layer (stamp before `insertMany`).
- [ ] **Step 6: Build, ctest 4/4**; before installing copy `build/install/release` to `D:\tmp\pre-layers-viewer` (for AM12); kill any viewer; install; confirm timestamps; smoke on CMU-1-Small-Region (back up its annotation file first): make Teaching active, draw a rectangle, hide Teaching, check the file is version 2; restore.
- [ ] **Step 7: CLAUDE.md** — a decision bullet (fixed default ids, the conditional version rule and why, resolution rule, counts ignore visibility, locked semantics, z-order from one `paintOrder`, delete-only-empty + merge, undo split); move "the annotation layers panel" out of "Not built yet"; note `kCurrentSchemaVersion` is now 2 in the persistence bullet.
- [ ] **Step 8: Commit** — `git commit -m "Add the Layers tab and layer behaviour in the viewer"`

### Task 5: Manual checks — group AM

**Files:** the spec's §10 (results, run record); `CLAUDE.md`.

- [ ] **Step 1: Run AM1–AM12** on CMU-1-Small-Region (back up and restore its annotation file; hash it for AM1). AM10 is a second window on the same slide; AM12 uses `D:\tmp\pre-layers-viewer` on a copy of a version-2 file and on a version-1 file this build wrote. Ask the owner before deleting `D:\tmp\pre-layers-viewer`.
- [ ] **Step 2: Record and commit** — `git commit -m "Record the group AM run"`
