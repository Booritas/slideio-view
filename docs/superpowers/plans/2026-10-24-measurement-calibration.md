# Measurement Calibration Override Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user set a per-scene pixel size that overrides the scanner's, and show on every measurement surface whether a number rests on metadata, a manual value, or nothing.

**Architecture:** A pure `core::effectiveCalibration` decides the calibration from the metadata and an optional stored override; a QSettings store keyed by (slide ID, scene) persists overrides; `MainWindow` computes one `core::Calibration` per scene and hands the same value to the scale bar, the properties panel, the Slide Properties panel and both exports.

**Tech Stack:** C++17, Qt 6.7 Widgets, Catch2 v3, CMake.

**Spec:** `docs/superpowers/specs/2026-10-24-measurement-calibration-design.md`

## Global Constraints

- Core stays Qt-free; nlohmann stays private to core; no floating-point `std::to_chars`/`std::from_chars` anywhere new (macOS 12 target).
- Source names are exactly `"metadata"`, `"manual"`, `"none"`.
- Valid override: both components finite and in **[0.01, 100] µm/px**, bounds inclusive.
- Metadata counts only when **both** axes are finite and positive; an override, when valid, wins over metadata.
- Store key: `calibration/overrides/<slideId>.s<sceneIndex>`, values `x`, `y` (µm/px) and `slideDisplayName`; an empty slide ID never gets an override.
- CSV: one new column `calibration_source`, immediately after `pixel_height_um`.
- GeoJSON: collection `slideio` block gains `calibrationSource`; `pixelSizeMicrons` only when the source is not `none`.
- Badge copy: **"manual calibration"**, **"uncalibrated"**; scale bar: **"(manual)"** suffix, **"Uncalibrated"** with no bar; open prompt: "This slide has no calibration — measurements are in pixels. Set one under View > Calibration…" (write `—` and `…` as `\xE2\x80\x94` / `\xE2\x80\xA6` byte escapes; MSVC has no `/utf-8`).
- Not on the undo stack. Works on read-only scenes. One spdlog info line per apply and per reset.
- New `.cpp` files must be added to their target's explicit source list in `src/<layer>/CMakeLists.txt`, and new tests to `tests/CMakeLists.txt`.

## Review Focus

1. **A scene switch inside a multi-scene file** — the calibration shown must be the new scene's, never the previous scene's override. Pinned by Task 2's per-scene key test and checked as AK4.
2. **An override equal to the metadata value** — still `manual` (the user decided), still badged. Pinned in Task 1.
3. **A stored override made invalid by hand-editing the registry** (0, negative, NaN, out of range) — ignored, falling back to metadata, never used. Pinned in Tasks 1 and 2.
4. **A slide that could not be identified** — the action is disabled and no override is read or written under an empty key. Pinned in Task 2 (`set` refuses an empty slide ID; `find` with an empty ID returns nothing).
5. **Exports after Reset to metadata** — the very next export writes `metadata` and the metadata pixel size, not the old manual one. Pinned by Task 3's tests on the context, and by AK5/AK10 through the single `Calibration` value MainWindow hands out.

---

### Task 1: The calibration rule and the store interface (core)

**Files:**
- Create: `src/core/include/slideio/viewer/core/Calibration.h`, `src/core/src/Calibration.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/CalibrationTest.cpp`

**Interfaces:**
- Produces (namespace `slideio::viewer::core`, includes `AnnotationMeasurement.h` for `PixelSize`):
  - `enum class CalibrationSource { Metadata, Manual, None };`
  - `struct Calibration { PixelSize micronsPerPixel{0.0, 0.0}; CalibrationSource source = CalibrationSource::None; };`
  - `inline constexpr double kMinCalibrationMicronsPerPixel = 0.01;` `inline constexpr double kMaxCalibrationMicronsPerPixel = 100.0;`
  - `[[nodiscard]] bool isValidCalibrationOverride(PixelSize micronsPerPixel);`
  - `[[nodiscard]] Calibration effectiveCalibration(double metadataXMetres, double metadataYMetres, std::optional<PixelSize> overrideMicrons);`
  - `[[nodiscard]] const char* calibrationSourceName(CalibrationSource source);`
  - `struct CalibrationOverride { std::string slideId; int sceneIndex = 0; PixelSize micronsPerPixel{0.0, 0.0}; std::string slideDisplayName; };`
  - `class ICalibrationOverrideStore { virtual ~…; virtual std::optional<CalibrationOverride> find(const std::string& slideId, int sceneIndex) const = 0; virtual bool set(const CalibrationOverride& entry) = 0; virtual void remove(const std::string& slideId, int sceneIndex) = 0; virtual std::vector<CalibrationOverride> all() const = 0; };` — `set` returns false and stores nothing for an empty slide ID or an invalid value.

- [ ] **Step 1: Write the failing tests** in `tests/core/CalibrationTest.cpp`, tag `[core][Calibration]`:
  - `"metadata on both axes is the calibration"`: `effectiveCalibration(0.25e-6, 0.26e-6, std::nullopt)` → source `Metadata`, `micronsPerPixel` ≈ {0.25, 0.26} (Approx).
  - `"a valid override wins over metadata"`: metadata 0.25e-6 both, override {0.5, 0.5} → `Manual`, {0.5, 0.5}.
  - `"an override equal to the metadata is still manual"`: metadata 0.5e-6, override {0.5, 0.5} → `Manual`.
  - `"one good metadata axis is not a calibration"`: (0.25e-6, 0.0) and (0.25e-6, NaN) → `None`, {0, 0}.
  - `"an invalid stored override is ignored"`: override {0, 0.5}, {-1, -1}, {NaN, 0.5}, {inf, 1}, {100.001, 1}, {0.0099, 1} each with metadata 0.25e-6 → `Metadata`; with metadata 0 → `None`.
  - `"the range is inclusive"`: `isValidCalibrationOverride({0.01, 100.0})` true; `{0.01, 100.0000001}` false.
  - `"source names"`: `"metadata"`, `"manual"`, `"none"`.
- [ ] **Step 2: Run** `cmake --build build/build --config Release --target slideio-viewer-core-tests` — Expected: compile error, `Calibration.h` not found.
- [ ] **Step 3: Implement** `Calibration.h/.cpp`; add `src/Calibration.cpp` to `src/core/CMakeLists.txt` and `core/CalibrationTest.cpp` to `tests/CMakeLists.txt`. Metres → µm is `× 1e6`. `std::isfinite` for finiteness.
- [ ] **Step 4: Run** `build/build/tests/Release/slideio-viewer-core-tests.exe "[Calibration]"` (core-tests is safe to run directly) — Expected: all pass. Then `ctest --test-dir build/build -C Release` — 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Add the calibration rule to core"`

### Task 2: The QSettings override store (infra)

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/QSettingsCalibrationOverrideStore.h`, `src/infra/src/QSettingsCalibrationOverrideStore.cpp`
- Modify: `src/infra/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/infra/QSettingsCalibrationOverrideStoreTest.cpp`

**Interfaces:**
- Consumes: Task 1's `ICalibrationOverrideStore`, `CalibrationOverride`, `isValidCalibrationOverride`.
- Produces: `class infra::QSettingsCalibrationOverrideStore : public core::ICalibrationOverrideStore` with `QSettingsCalibrationOverrideStore()` (application settings) and `explicit QSettingsCalibrationOverrideStore(const std::string& iniFilePath)` (tests) — the same shape as `QSettingsColorProfileOverrideStore`.

- [ ] **Step 1: Write the failing tests** (`[infra][CalibrationStore]`, each over a `QTemporaryDir` INI as the ICC store's tests do):
  - `"an override survives a second store over the same file"`: set {slideId "a3f8c2e109b74d21", scene 0, {0.5, 0.25}, "case001.svs"}; a new store over the same INI finds x 0.5, y 0.25, the display name.
  - `"an override is per scene"`: set for scene 0; `find(id, 1)` is empty.
  - `"remove forgets it"`: set, remove, find → empty; `all()` empty.
  - `"set refuses an empty slide id and an invalid value"`: `set` with "" → false, `all()` empty; with {0, 0.5} → false; with {250, 250} → false.
  - `"a hand-edited invalid value is not returned"`: write `calibration/overrides/<id>.s0/x = 0` directly with `QSettings` over the INI; `find` → empty.
  - `"find with an empty slide id finds nothing"`.
- [ ] **Step 2: Run** `cmake --build build/build --config Release --target slideio-viewer-infra-tests` — Expected: compile error, header not found.
- [ ] **Step 3: Implement** the store; group `calibration/overrides`, child key `<slideId>.s<sceneIndex>`; `all()` parses the child key back into slide ID and scene and skips anything that does not parse or is invalid.
- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release -R infra-tests --output-on-failure` — Expected: pass; then the full ctest 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Store calibration overrides per scene"`

### Task 3: Both exports carry the calibration source

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationMeasurementExport.h`, `src/ui/src/AnnotationMeasurementExport.cpp`, `src/core/include/slideio/viewer/core/AnnotationGeoJson.h`, `src/core/src/AnnotationGeoJson.cpp`
- Test: `tests/ui/AnnotationMeasurementExportTest.cpp`, `tests/core/AnnotationGeoJsonTest.cpp`

**Interfaces:**
- Consumes: Task 1's `CalibrationSource`, `calibrationSourceName`.
- Produces: `MeasurementExportContext::calibrationSource` and `GeoJsonExportContext::calibrationSource`, both `core::CalibrationSource`, default `None`. The existing pixel-size fields keep their meaning: they are the pixel size **used** (the manual one when the source is `Manual`); MainWindow fills them from the `Calibration` in Task 4.

- [ ] **Step 1: Write the failing tests**:
  - CSV `"the header names the calibration source after the pixel size"`: the header line contains `pixel_height_um,calibration_source,slide`.
  - CSV `"each row says where its calibration came from"`: context with source `Manual`, pixel 0.5/0.5 → a rectangle row ends its calibration fields `…,0.5,0.5,manual,…`; source `None` with pixel 0/0 → micron columns empty and `none`; `Metadata` → `metadata`.
  - GeoJSON `"the collection names its calibration source"`: `Manual` with 0.5/0.5 → `slideio.calibrationSource == "manual"` and `pixelSizeMicrons == [0.5, 0.5]`; `None` → `"none"` and no `pixelSizeMicrons` key.
  - Update any existing test that asserts the full CSV header text.
- [ ] **Step 2: Run** the ui and core test targets' build — Expected: compile errors on the missing `calibrationSource` members.
- [ ] **Step 3: Implement**: the CSV writes `calibrationSourceName(context.calibrationSource)` as a plain text cell (through `csvField` — it is text, never a number) after `pixel_height_um`; GeoJSON writes `"calibrationSource"` in the collection's `slideio` block always.
- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release --output-on-failure` — Expected: 4/4.
- [ ] **Step 5: Commit** — `git commit -m "Name the calibration source in both exports"`

### Task 4: The dialog, the badges, the scale bar and the wiring

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/CalibrationPolicy.h`, `src/ui/src/CalibrationPolicy.cpp`, `src/ui/include/slideio/viewer/ui/CalibrationDialog.h`, `src/ui/src/CalibrationDialog.cpp`
- Modify: `src/ui/src/MainWindow.cpp`, `src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h`, `src/ui/src/AnnotationPropertiesPanel.cpp`, `src/ui/include/slideio/viewer/ui/ZoomIndicatorWidget.h`, `src/ui/src/ZoomIndicatorWidget.cpp`, `src/ui/include/slideio/viewer/ui/ScaleBarWidget.h`, `src/ui/src/ScaleBarWidget.cpp`, `src/ui/include/slideio/viewer/ui/SlidePropertiesPanel.h`, `src/ui/src/SlidePropertiesPanel.cpp`, `src/ui/CMakeLists.txt` (sources are listed; headers are globbed), `tests/CMakeLists.txt`, `CLAUDE.md`
- Test: `tests/ui/CalibrationPolicyTest.cpp`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces (namespace `slideio::viewer::ui`, pure, no widgets):
  - `[[nodiscard]] std::string calibrationBadgeText(core::CalibrationSource source);` → `""`, `"manual calibration"`, `"uncalibrated"`.
  - `[[nodiscard]] std::string scaleBarSuffix(core::CalibrationSource source);` → `""`, `" (manual)"`, `""` (None draws "Uncalibrated" instead of a bar).
  - `struct CalibrationPrefill { std::optional<core::PixelSize> value; bool squarePixels = true; };` and `[[nodiscard]] CalibrationPrefill calibrationPrefill(std::optional<core::PixelSize> currentOverride, double metadataXMetres, double metadataYMetres);` — override, else metadata (µm), else empty; `squarePixels` true unless the prefilled X and Y differ.
  - `[[nodiscard]] std::string calibrationInputError(double x, double y);` — empty when `isValidCalibrationOverride({x, y})`, else "Enter a pixel size between 0.01 and 100 µm per pixel." (µ as `\xC2\xB5`).
  - `AnnotationPropertiesPanel::setCalibration(const core::Calibration&)` **replaces** `setPixelSize`; the panel converts µm to metres internally and shows `calibrationBadgeText` beside the measurement rows with a tooltip naming the value and source.
  - `ZoomIndicatorWidget::setCalibration(const core::Calibration&)` replaces `setResolution`; `ScaleBarWidget::setScale` gains the source and draws "Uncalibrated" plus `QStyle::SP_MessageBoxWarning` for `None`.
  - `SlidePropertiesPanel::setCalibration(const core::Calibration&)` adds the "Calibration" row of spec §6.

- [ ] **Step 1: Write the failing tests** (`[ui][CalibrationPolicy]`):
  - `"the badge names a manual or missing calibration"`: Metadata → `""`; Manual → `"manual calibration"`; None → `"uncalibrated"`.
  - `"the scale bar marks a manual calibration"`: Manual → `" (manual)"`; others `""`.
  - `"the dialog prefills the override, then the metadata, then nothing"`: override {0.5, 0.5} + metadata 0.25e-6 → {0.5, 0.5}; no override + metadata 0.25e-6/0.26e-6 → ≈{0.25, 0.26} and `squarePixels == false`; neither → no value, `squarePixels == true`.
  - `"an out-of-range entry names the range"`: (250, 250) and (0, 0.5) → message contains "0.01" and "100"; (0.5, 0.5) → empty.
- [ ] **Step 2: Run** the ui-tests build — Expected: compile error, `CalibrationPolicy.h` not found.
- [ ] **Step 3: Implement the policy** and make the tests pass (`ctest -R ui-tests`).
- [ ] **Step 4: Implement the dialog and wiring** — no test reaches it:
  - `MainWindow::Impl` owns a `std::unique_ptr<infra::QSettingsCalibrationOverrideStore>` and a `core::Calibration currentCalibration`, recomputed by one `refreshCalibration()` on slide/scene open (where `setPixelSize` is called today, `MainWindow.cpp:~1013`) and after the dialog applies; `refreshCalibration()` pushes the value to the zoom indicator, the properties panel and the Slide Properties panel, and is the only reader of `info.resolutionX/Y` for measurement — the export contexts (`MainWindow.cpp:~2115` and `~2187`) take `currentCalibration` instead of `info.resolutionX/Y * 1e6`.
  - `viewMenu` gains `calibrationAction` ("Calibration…") right after `manageSlideProfilesAction`; enabled exactly when a slide is open **and** `currentSlideId()` is non-empty (status tip otherwise: "This slide could not be identified, so a calibration cannot be stored for it"); paired with `closeAction` at every site it is enabled/disabled; **not** in `applyAnnotationAvailability`.
  - `CalibrationDialog(metadata x/y metres, current override, parent)`: fields per spec §5 with `QDoubleSpinBox` (4 decimals, range 0.0001–1000 so the policy, not the spin box, refuses and names the range), Square pixels check box, Reset to metadata button (disabled without an override), OK validated through `calibrationInputError`. Result: apply value, reset, or cancel.
  - On apply/reset: store `set`/`remove`, `spdlog::info("Calibration for {} scene {}: {} {}x{} -> {} {}x{}", …)`, `refreshCalibration()`, update the properties panel's measurements immediately.
  - On opening a scene whose source is `None`: `statusBarManager->showTransientMessage(<prompt copy>)`.
- [ ] **Step 5: Build, ctest 4/4, install** (kill any viewer first; compare installed and built exe timestamps), then smoke: open CMU-1-Small-Region, View > Calibration…, set 0.5, see the badge and "(manual)", Reset.
- [ ] **Step 6: CLAUDE.md** — a "Measurement calibration" decision bullet (source rule, per-scene QSettings store and why not the annotation file, one `Calibration` handed to every surface, CSV column), and remove nothing else.
- [ ] **Step 7: Commit** — `git commit -m "Add the calibration override dialog and show the source everywhere"`

### Task 5: Manual checks — group AK

**Files:** the spec's §9 (results and run record); `CLAUDE.md` (checks run, result).

- [ ] **Step 1: Run AK1–AK11.** Back up the QSettings `calibration` group (export `HKCU\Software\SlideIO\SlideIO Viewer\calibration` with `reg export`) and CMU's annotation file first; restore both after. AK4 needs a multi-scene file — use the one behind `156dd45028161986.s0/.s1` in the workspace. AK9's read-only scene is a second instance on the same slide; hash the annotation file before and after.
- [ ] **Step 2: Record and commit** — `git commit -m "Record the group AK run"`
