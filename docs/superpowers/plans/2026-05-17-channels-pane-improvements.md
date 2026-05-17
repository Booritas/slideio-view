# Channels Pane Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the existing right-dock `ChannelMixerPanel` with a tri-state master "All channels" toggle, per-channel histograms (computed once at slide open from a downsampled `Scene::readBlock`), draggable min/max handles on each channel's histogram, and a renderer wiring change that makes the per-channel display range apply to both fluorescence and brightfield slides uniformly.

**Architecture:** Histogram bins live in `core::ChannelInfo` (pure C++), populated by the existing autodetect pass in `ViewportWidget`. A new `ChannelHistogramView` Qt widget owns the histogram paint and the two draggable handles. The `ChannelMixerPanel` gains a per-row disclosure triangle that reveals the histogram + min/max controls + Auto/Reset/Log-Lin buttons. Renderer gating is decoupled from `displayRange.autoDetected` via a new `userOverrideRange` flag on `ChannelInfo`.

**Tech Stack:** C++17, Qt 6 Widgets (`QCheckBox`, `QToolButton`, `QLineEdit`, `QDoubleValidator`, custom `QWidget` paint), CMake, Catch2 (for the new core histogram unit tests), SlideIO library (read via the existing infra adapter).

**Reference spec:** `docs/superpowers/specs/2026-05-17-channels-pane-improvements-design.md`

---

## File map

**Create:**
- `src/core/include/slideio/viewer/core/Histogram.h` — declares `computeHistogramStrided(...)` over a `DataType`-typed buffer.
- `src/core/src/Histogram.cpp` — runtime dispatch + `scanHistogramStrided<T>` template implementation.
- `src/ui/include/slideio/viewer/ui/ChannelHistogramView.h` — custom histogram widget public header.
- `src/ui/src/ChannelHistogramView.cpp` — widget implementation (paint, mouse, log/linear).
- `tests/core/HistogramTest.cpp` — Catch2 unit tests for histogram math.

**Modify:**
- `src/core/include/slideio/viewer/core/Types.h` — add `ChannelHistogram` struct, `kNumBins` constant, three new `ChannelInfo` fields.
- `src/core/CMakeLists.txt` — add `src/Histogram.cpp` to the static library sources.
- `src/ui/CMakeLists.txt` — add `src/ChannelHistogramView.cpp` to the UI sources.
- `src/ui/src/ViewportWidget.cpp` — bump `kMaxThumbDim` to 1000; run the existing `readBlock` unconditionally for histograms; populate `histogram` and `autoDisplayRange` per channel; change the renderer gating to `userOverrideRange || displayRange.autoDetected`; preserve `userOverrideRange` in `setChannelSettings`.
- `src/ui/src/ChannelMixerPanel.cpp` — disclosure triangle + per-row expansion; embed `ChannelHistogramView` + min/max textboxes + Auto/Reset/Log-Lin buttons in the expanded section; tri-state master "All channels" checkbox with restore-previous semantics + "N visible" label.
- `tests/CMakeLists.txt` — add `HistogramTest.cpp` to the `slideio-viewer-core-tests` target sources.

**No changes:**
- `src/ui/include/slideio/viewer/ui/ChannelMixerPanel.h` — public API (`setChannels`, `clearChannels`, `channelSettingsChanged`) is unchanged.
- `src/ui/src/MainWindow.cpp` — already forwards `channelSettingsChanged` to `ViewportWidget::setChannelSettings`.
- `tests/ui/` — no UI test target exists; following the existing precedent the widget changes are validated by a manual smoke checklist (see Task 14).

---

## Build & test commands (reference for every task)

Build:
```
cmake --build build/build --config Release
```

Run core unit tests:
```
build/build/tests/Release/slideio-viewer-core-tests.exe --reporter console
```

Or via CTest from the project root:
```
ctest --test-dir build/build -C Release --output-on-failure
```

---

## Task 1: Add `ChannelHistogram` struct and `kNumBins` to core `Types.h`

**Files:**
- Modify: `src/core/include/slideio/viewer/core/Types.h`

- [ ] **Step 1: Add `kNumBins` constant near the other constants**

Open `src/core/include/slideio/viewer/core/Types.h`. After the `enum class DataType` block (which ends with `None` at line 28, closing brace at line 29), add:

```cpp
/// Number of bins used for per-channel histograms. Byte data gets exactly
/// one bin per code; higher-precision data is quantized.
constexpr int kNumBins = 256;
```

- [ ] **Step 2: Add the `ChannelHistogram` struct after the `DisplayRange` struct**

Locate the `struct DisplayRange { ... };` block (lines 51–56). Immediately after its closing `};`, insert:

```cpp
struct ChannelHistogram
{
    std::vector<uint32_t> bins;    // size == kNumBins when valid; empty otherwise
    double rangeMin = 0.0;         // pixel value mapped to bins[0]
    double rangeMax = 0.0;         // pixel value mapped to bins.back()
    uint64_t totalSamples = 0;     // sum of bins[]; used to guard log-axis scaling
    bool valid = false;            // false if computation failed or was skipped
};
```

- [ ] **Step 3: Extend `ChannelInfo` with the three new fields**

Locate `struct ChannelInfo` (line 58). The struct currently ends with `DisplayRange displayRange;`. Replace that line with:

```cpp
    DisplayRange displayRange;
    DisplayRange autoDisplayRange;   // frozen snapshot of autodetect result; "Auto" button restores from this
    ChannelHistogram histogram;      // populated once at slide open
    bool userOverrideRange = false;  // true when user has edited min/max or clicked Reset
```

- [ ] **Step 4: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds. No targets use the new fields yet, but every TU that includes `Types.h` recompiles.

- [ ] **Step 5: Commit**

```
git add src/core/include/slideio/viewer/core/Types.h
git commit -m "Add ChannelHistogram type and ChannelInfo fields for histogram + user-override range"
```

---

## Task 2: Create the `Histogram.h` declaration

**Files:**
- Create: `src/core/include/slideio/viewer/core/Histogram.h`

- [ ] **Step 1: Create `Histogram.h`**

Write `src/core/include/slideio/viewer/core/Histogram.h` with this exact content:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "slideio/viewer/core/Types.h"

namespace slideio::viewer::core
{

/// Bin a single channel of an interleaved pixel buffer into the supplied bin
/// array. The buffer is assumed to have `pixelCount` pixels, each storing
/// `numChannels` interleaved elements of `dataType`. Only the element at
/// `channelIndex` of every pixel is read. Values are mapped linearly from
/// `[rangeMin, rangeMax]` to `[0, numBins)` and clamped at the edges.
///
/// `bins` MUST point to `numBins` uint32_t cells; the function increments the
/// cells in place — it does NOT zero them, so callers should zero-initialize.
///
/// No-op if `pixelCount == 0`, `numBins <= 0`, or `rangeMax <= rangeMin`.
void computeHistogramStrided(const uint8_t* data, size_t pixelCount,
                             int numChannels, int channelIndex,
                             DataType dataType,
                             double rangeMin, double rangeMax,
                             uint32_t* bins, int numBins);

} // namespace slideio::viewer::core
```

- [ ] **Step 2: Build (verifies the header parses)**

```
cmake --build build/build --config Release
```

Expected: build succeeds. No source uses the declaration yet.

- [ ] **Step 3: Commit**

```
git add src/core/include/slideio/viewer/core/Histogram.h
git commit -m "Declare core::computeHistogramStrided for per-channel binning"
```

---

## Task 3: Add `HistogramTest.cpp` with failing tests (TDD)

**Files:**
- Create: `tests/core/HistogramTest.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test file**

Create `tests/core/HistogramTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "slideio/viewer/core/Histogram.h"
#include "slideio/viewer/core/Types.h"

using namespace slideio::viewer::core;

TEST_CASE("Histogram of a Byte buffer counts every pixel", "[core][Histogram]")
{
    // 4 pixels, single channel, values 0, 64, 128, 255.
    const std::array<uint8_t, 4> data = {0, 64, 128, 255};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(data.data(), data.size(), 1, 0,
                            DataType::Byte, 0.0, 255.0,
                            bins.data(), kNumBins);

    uint64_t sum = 0;
    for (auto v : bins) sum += v;
    REQUIRE(sum == data.size());

    // Byte range maps exactly: 0 -> bin 0, 255 -> bin 255 (clamped).
    REQUIRE(bins[0] == 1);
    REQUIRE(bins[64] == 1);
    REQUIRE(bins[128] == 1);
    REQUIRE(bins[255] == 1);
}

TEST_CASE("Histogram reads only the requested channel for interleaved data",
          "[core][Histogram]")
{
    // 3 pixels, 2 channels interleaved as [ch0,ch1, ch0,ch1, ch0,ch1].
    // Channel 0 values: 10, 20, 30. Channel 1 values: 200, 210, 220.
    const std::array<uint8_t, 6> data = {10, 200, 20, 210, 30, 220};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(data.data(), /*pixelCount=*/3, /*numChannels=*/2,
                            /*channelIndex=*/0,
                            DataType::Byte, 0.0, 255.0,
                            bins.data(), kNumBins);

    REQUIRE(bins[10] == 1);
    REQUIRE(bins[20] == 1);
    REQUIRE(bins[30] == 1);
    REQUIRE(bins[200] == 0);
    REQUIRE(bins[210] == 0);
    REQUIRE(bins[220] == 0);
}

TEST_CASE("Histogram clamps out-of-range Float32 values to edge bins",
          "[core][Histogram]")
{
    // Float32 buffer: -0.5, 0.0, 0.5, 1.0, 2.0. Range is [0, 1].
    const std::array<float, 5> data = {-0.5f, 0.0f, 0.5f, 1.0f, 2.0f};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), 1, 0,
                            DataType::Float32, 0.0, 1.0,
                            bins.data(), kNumBins);

    // -0.5 and 0.0 both clamp to bin 0; 1.0 and 2.0 both clamp to bin 255.
    REQUIRE(bins[0] == 2);
    REQUIRE(bins[kNumBins - 1] == 2);
    REQUIRE(bins[128] == 1);  // 0.5 maps to mid-bin
}

TEST_CASE("Histogram bins UInt16 values across the full type range",
          "[core][Histogram]")
{
    // UInt16 values: 0, 32768, 65535 -> bins 0, 128, 255.
    const std::array<uint16_t, 3> data = {0u, 32768u, 65535u};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), 1, 0,
                            DataType::UInt16, 0.0, 65535.0,
                            bins.data(), kNumBins);

    REQUIRE(bins[0] == 1);
    REQUIRE(bins[128] == 1);
    REQUIRE(bins[kNumBins - 1] == 1);
}

TEST_CASE("Histogram of an empty buffer leaves bins untouched", "[core][Histogram]")
{
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(nullptr, 0, 1, 0,
                            DataType::Byte, 0.0, 255.0,
                            bins.data(), kNumBins);

    for (auto v : bins) {
        REQUIRE(v == 0);
    }
}

TEST_CASE("Histogram is a no-op when rangeMax <= rangeMin", "[core][Histogram]")
{
    const std::array<uint8_t, 3> data = {1, 2, 3};
    std::vector<uint32_t> bins(kNumBins, 0);

    computeHistogramStrided(data.data(), data.size(), 1, 0,
                            DataType::Byte, 5.0, 5.0,
                            bins.data(), kNumBins);

    for (auto v : bins) {
        REQUIRE(v == 0);
    }
}
```

- [ ] **Step 2: Wire the test into the core test target**

Open `tests/CMakeLists.txt`. The `slideio-viewer-core-tests` `add_executable` block currently lists four files. Add `core/HistogramTest.cpp` so the block reads:

```cmake
add_executable(slideio-viewer-core-tests
    core/TileKeyTest.cpp
    core/TilePyramidTest.cpp
    core/ViewportTest.cpp
    core/CoordinateSystemTest.cpp
    core/HistogramTest.cpp
)
```

- [ ] **Step 3: Build and confirm the tests fail to link**

```
cmake --build build/build --config Release
```

Expected: build FAILS with a linker error referencing `computeHistogramStrided` (because we declared it in Task 2 but haven't implemented it yet). This is the "red" of TDD — proceed to Task 4.

If instead the build succeeds, the tests will run and FAIL with non-zero bin counts not being produced. Either failure mode is acceptable as the "red" state.

- [ ] **Step 4: Commit the failing tests**

```
git add tests/core/HistogramTest.cpp tests/CMakeLists.txt
git commit -m "Add failing Catch2 tests for core::computeHistogramStrided"
```

---

## Task 4: Implement `computeHistogramStrided` and make tests pass

**Files:**
- Create: `src/core/src/Histogram.cpp`
- Modify: `src/core/CMakeLists.txt`

- [ ] **Step 1: Create `Histogram.cpp`**

Write `src/core/src/Histogram.cpp`:

```cpp
#include "slideio/viewer/core/Histogram.h"

#include <cstdint>

namespace slideio::viewer::core
{

namespace
{

template <typename T>
void scanHistogramStrided(const void* dataPtr, size_t pixelCount,
                          int numChannels, int channelIndex,
                          double rangeMin, double rangeMax,
                          uint32_t* bins, int numBins)
{
    const T* typed = static_cast<const T*>(dataPtr);
    const double span = rangeMax - rangeMin;
    const double scale = static_cast<double>(numBins) / span;
    const size_t stride = static_cast<size_t>(numChannels);
    const size_t offset = static_cast<size_t>(channelIndex);

    for (size_t i = 0; i < pixelCount; ++i) {
        const T raw = typed[i * stride + offset];
        const double v = static_cast<double>(raw);
        int idx = static_cast<int>((v - rangeMin) * scale);
        if (idx < 0) idx = 0;
        else if (idx >= numBins) idx = numBins - 1;
        bins[idx]++;
    }
}

} // namespace

void computeHistogramStrided(const uint8_t* data, size_t pixelCount,
                             int numChannels, int channelIndex,
                             DataType dataType,
                             double rangeMin, double rangeMax,
                             uint32_t* bins, int numBins)
{
    if (pixelCount == 0 || numBins <= 0 || rangeMax <= rangeMin) {
        return;
    }
    if (bins == nullptr || data == nullptr) {
        return;
    }
    if (channelIndex < 0 || channelIndex >= numChannels) {
        return;
    }

    switch (dataType) {
        case DataType::Byte:
            scanHistogramStrided<uint8_t>(data, pixelCount, numChannels, channelIndex,
                                          rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Int8:
            scanHistogramStrided<int8_t>(data, pixelCount, numChannels, channelIndex,
                                         rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::UInt16:
            scanHistogramStrided<uint16_t>(data, pixelCount, numChannels, channelIndex,
                                           rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Int16:
            scanHistogramStrided<int16_t>(data, pixelCount, numChannels, channelIndex,
                                          rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::UInt32:
            scanHistogramStrided<uint32_t>(data, pixelCount, numChannels, channelIndex,
                                           rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Int32:
            scanHistogramStrided<int32_t>(data, pixelCount, numChannels, channelIndex,
                                          rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Float32:
            scanHistogramStrided<float>(data, pixelCount, numChannels, channelIndex,
                                        rangeMin, rangeMax, bins, numBins);
            break;
        case DataType::Float64:
            scanHistogramStrided<double>(data, pixelCount, numChannels, channelIndex,
                                         rangeMin, rangeMax, bins, numBins);
            break;
        default:
            break;
    }
}

} // namespace slideio::viewer::core
```

- [ ] **Step 2: Add `Histogram.cpp` to the core target sources**

Open `src/core/CMakeLists.txt`. Replace:

```cmake
add_library(slideio-viewer-core STATIC
    src/TileKey.cpp
    src/TilePyramid.cpp
    src/Viewport.cpp
    src/CoordinateSystem.cpp
)
```

With:

```cmake
add_library(slideio-viewer-core STATIC
    src/TileKey.cpp
    src/TilePyramid.cpp
    src/Viewport.cpp
    src/CoordinateSystem.cpp
    src/Histogram.cpp
)
```

- [ ] **Step 3: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds.

- [ ] **Step 4: Run the core tests**

```
build/build/tests/Release/slideio-viewer-core-tests.exe --reporter console
```

Expected: all tests pass, including the six new `[core][Histogram]` cases. If any fail, fix the implementation; do not edit the tests to match.

- [ ] **Step 5: Commit**

```
git add src/core/src/Histogram.cpp src/core/CMakeLists.txt
git commit -m "Implement core::computeHistogramStrided with per-DataType dispatch"
```

---

## Task 5: Wire histogram computation into `ViewportWidget::computeChannelDisplayRanges`

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp`

- [ ] **Step 1: Include the histogram header**

Open `src/ui/src/ViewportWidget.cpp`. Near the top of the file, with the other `#include "slideio/viewer/core/..."` lines, add:

```cpp
#include "slideio/viewer/core/Histogram.h"
```

- [ ] **Step 2: Add a file-local helper that returns the data-type range for binning**

In the existing anonymous namespace near the other helpers (`computeMinMaxStrided`, `computeMinMax`, `mapPixelNormalized`), add this new function. Place it directly after the `computeMinMax` function (which currently ends around line 371):

```cpp
// Full data-type range used for per-channel histogram binning. Matches the
// fallback ranges in setSlideOpenResult around line 1022 so handle math in
// the UI lines up with renderer's toSamplerRange normalization.
std::pair<double, double> dataTypeHistogramRange(DataType dt)
{
    switch (dt) {
        case DataType::Byte:    return {0.0, 255.0};
        case DataType::Int8:    return {-128.0, 127.0};
        case DataType::UInt16:  return {0.0, 65535.0};
        case DataType::Int16:   return {0.0, 32767.0};
        case DataType::UInt32:  return {0.0, 4294967295.0};
        case DataType::Int32:   return {0.0, 2147483647.0};
        case DataType::Float32: return {0.0, 1.0};
        case DataType::Float64: return {0.0, 1.0};
        default:                return {0.0, 255.0};
    }
}
```

- [ ] **Step 3: Bump `kMaxThumbDim` from 800 to 1000**

Find line `constexpr int kMaxThumbDim = 800;` (around line 662). Change it to:

```cpp
constexpr int kMaxThumbDim = 1000;
```

- [ ] **Step 4: Compute `preferredT` alongside `preferredZ`**

After the line `const int preferredZ = numZ > 1 ? numZ / 2 : 0;` (around line 698), add:

```cpp
const int numT = std::max(1, slideInfo.numTFrames);
const int preferredT = numT > 1 ? numT / 2 : 0;
```

- [ ] **Step 5: Pass `preferredT` to the readBlock call**

Find the block:

```cpp
auto thumbLoan = pool.acquire();
auto blockData = thumbLoan->readBlock(0, 0, slideInfo.width, slideInfo.height,
                                      thumbW, thumbH, preferredZ);
```

(around lines 766–768). `SlideIOAdapter::readBlock` accepts `zIndex` and `tFrame` (see `src/infra/include/slideio/viewer/infra/SlideIOAdapter.h:48-50`). Change the call to:

```cpp
auto thumbLoan = pool.acquire();
auto blockData = thumbLoan->readBlock(0, 0, slideInfo.width, slideInfo.height,
                                      thumbW, thumbH, preferredZ, preferredT);
```

The autodetect path that consumes `blockData` is unchanged; the only effect is that the histogram pass reads from the middle T slice on multi-T stacks.

- [ ] **Step 6: After the autodetect fallback block, compute per-channel histograms**

Find the closing `}` of the `if (!slideInfo.displayRange.autoDetected && blockUsable) { ... }` block (around line 806). Immediately after that closing brace, add:

```cpp
// Per-channel histograms. Always computed from the readBlock buffer when
// usable; uses the data-type's full range so draggable-handle math in the
// UI lines up with the renderer's toSamplerRange normalization.
if (blockUsable && numCh >= 1) {
    const int blockCh = blockData.numChannels();
    const size_t blockPixels = static_cast<size_t>(blockData.width())
                             * static_cast<size_t>(blockData.height());

    for (int ch = 0; ch < numCh; ++ch) {
        const size_t ch_z = static_cast<size_t>(ch);
        if (ch_z >= slideInfo.channels.size()) break;
        if (ch >= blockCh) continue;

        auto& chInfo = slideInfo.channels[ch_z];
        auto [rMin, rMax] = dataTypeHistogramRange(chInfo.dataType);

        chInfo.histogram.bins.assign(core::kNumBins, 0);
        chInfo.histogram.rangeMin = rMin;
        chInfo.histogram.rangeMax = rMax;

        core::computeHistogramStrided(blockData.buffer().data(), blockPixels,
                                      blockCh, ch,
                                      chInfo.dataType, rMin, rMax,
                                      chInfo.histogram.bins.data(),
                                      core::kNumBins);

        uint64_t total = 0;
        for (uint32_t v : chInfo.histogram.bins) total += v;
        chInfo.histogram.totalSamples = total;
        chInfo.histogram.valid = (total > 0);
    }
}

// Freeze the autodetect result for the "Auto" button. Done after both the
// coarse-tile and block-fallback passes so it captures the final values.
for (auto& chInfo : slideInfo.channels) {
    chInfo.autoDisplayRange = chInfo.displayRange;
}
```

- [ ] **Step 7: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds.

- [ ] **Step 8: Run the core tests to confirm no regression**

```
build/build/tests/Release/slideio-viewer-core-tests.exe --reporter console
```

Expected: all tests still pass.

- [ ] **Step 9: Commit**

```
git add src/ui/src/ViewportWidget.cpp
git commit -m "Compute per-channel histograms in autodetect pass; snapshot autoDisplayRange"
```

---

## Task 6: Renderer gating change + `setChannelSettings` preserves `userOverrideRange`

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp`

- [ ] **Step 1: Update the renderer gating condition**

Find the line in the `drawFluorescenceTile` lambda (around line 2531):

```cpp
const bool useChannelRange = chInfo.displayRange.autoDetected;
```

Replace with:

```cpp
const bool useChannelRange = chInfo.userOverrideRange || chInfo.displayRange.autoDetected;
```

- [ ] **Step 2: Confirm `setChannelSettings` already round-trips the new fields**

Open `src/ui/src/ViewportWidget.cpp` at line 2133. The function body (lines 2142–2145) performs a whole-vector struct assignment:

```cpp
if (channels.size() == m_impl->slideInfo.channels.size()) {
    m_impl->slideInfo.channels = channels;
    update();
}
```

This already copies every field of every `ChannelInfo`, including the three new ones (`userOverrideRange`, `autoDisplayRange`, `histogram`). No code change is required.

Per the spec invariant (§3.4), the panel reads `histogram` and `autoDisplayRange` from the slide-open payload and never mutates them, so the struct assignment safely round-trips them back. No defensive code is needed.

- [ ] **Step 3: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds.

- [ ] **Step 4: Manual rendering sanity check**

Run the viewer (`build/build/Release/slideio-viewer.exe`), open both a fluorescence slide and a brightfield (RGB) slide that you used previously. Verify:

- Fluorescence slide renders identically to before (per-channel ranges from autodetect are still being used because `autoDisplayRange.autoDetected` is true).
- Brightfield slide now also reads from per-channel ranges (the autodetect already sets `autoDetected = true` for `numCh > 1`; the gating change activates them). It should look visually similar — colors may shift slightly compared to the previous slide-wide range if per-channel autodetected ranges differ across R/G/B.

If brightfield colors look broken (e.g., one channel saturates), the per-channel autodetect for brightfield is producing problematic values. In that case, revert this single line and add a brightfield-specific guard (out of scope for this plan — file a follow-up).

- [ ] **Step 5: Commit**

```
git add src/ui/src/ViewportWidget.cpp
git commit -m "Use per-channel display range whenever user-set or autodetected"
```

---

## Task 7: Create the `ChannelHistogramView` widget

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/ChannelHistogramView.h`
- Create: `src/ui/src/ChannelHistogramView.cpp`
- Modify: `src/ui/CMakeLists.txt`

- [ ] **Step 1: Create the public header**

Write `src/ui/include/slideio/viewer/ui/ChannelHistogramView.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Types.h"

#include <QWidget>

namespace slideio::viewer::ui
{

class ChannelHistogramView : public QWidget
{
    Q_OBJECT
public:
    explicit ChannelHistogramView(QWidget* parent = nullptr);

    void setHistogram(const core::ChannelHistogram& h);
    void setDisplayRange(double minVal, double maxVal);
    void setChannelColor(float r, float g, float b);
    void setLogScale(bool log);

signals:
    void displayRangeChanged(double minVal, double maxVal);
    void displayRangeCommitted(double minVal, double maxVal);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    enum class Handle { None, Min, Max };

    Handle hitHandle(int x) const;
    int valueToX(double v) const;
    double xToValue(int x) const;

    core::ChannelHistogram m_histogram;
    double m_displayMin = 0.0;
    double m_displayMax = 0.0;
    float m_colorR = 1.0f;
    float m_colorG = 1.0f;
    float m_colorB = 1.0f;
    bool m_logScale = true;
    Handle m_dragHandle = Handle::None;
};

} // namespace slideio::viewer::ui
```

- [ ] **Step 2: Create the implementation**

Write `src/ui/src/ChannelHistogramView.cpp`:

```cpp
#include "slideio/viewer/ui/ChannelHistogramView.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace slideio::viewer::ui
{

namespace
{
constexpr int kHandleHitWidth = 8;
constexpr int kHandleVisualWidth = 2;
constexpr int kFixedHeight = 60;

double clampToRange(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
} // namespace

ChannelHistogramView::ChannelHistogramView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(kFixedHeight);
    setMaximumHeight(kFixedHeight);
    setCursor(Qt::ArrowCursor);
}

void ChannelHistogramView::setHistogram(const core::ChannelHistogram& h)
{
    m_histogram = h;
    if (m_displayMin == 0.0 && m_displayMax == 0.0) {
        m_displayMin = h.rangeMin;
        m_displayMax = h.rangeMax;
    }
    update();
}

void ChannelHistogramView::setDisplayRange(double minVal, double maxVal)
{
    m_displayMin = minVal;
    m_displayMax = maxVal;
    update();
}

void ChannelHistogramView::setChannelColor(float r, float g, float b)
{
    m_colorR = r;
    m_colorG = g;
    m_colorB = b;
    update();
}

void ChannelHistogramView::setLogScale(bool log)
{
    m_logScale = log;
    update();
}

QSize ChannelHistogramView::sizeHint() const
{
    return {200, kFixedHeight};
}

int ChannelHistogramView::valueToX(double v) const
{
    const double span = m_histogram.rangeMax - m_histogram.rangeMin;
    if (span <= 0.0 || width() <= 0) return 0;
    const double frac = (v - m_histogram.rangeMin) / span;
    const double xd = clampToRange(frac, 0.0, 1.0) * (width() - 1);
    return static_cast<int>(std::round(xd));
}

double ChannelHistogramView::xToValue(int x) const
{
    if (width() <= 1) return m_histogram.rangeMin;
    const double frac = clampToRange(
        static_cast<double>(x) / static_cast<double>(width() - 1), 0.0, 1.0);
    return m_histogram.rangeMin + frac * (m_histogram.rangeMax - m_histogram.rangeMin);
}

ChannelHistogramView::Handle ChannelHistogramView::hitHandle(int x) const
{
    const int minX = valueToX(m_displayMin);
    const int maxX = valueToX(m_displayMax);
    if (std::abs(x - minX) <= kHandleHitWidth / 2 + 2) return Handle::Min;
    if (std::abs(x - maxX) <= kHandleHitWidth / 2 + 2) return Handle::Max;
    return Handle::None;
}

void ChannelHistogramView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    p.fillRect(rect(), QColor(0x25, 0x25, 0x25));

    if (!m_histogram.valid || m_histogram.bins.empty()) {
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Histogram unavailable"));
        return;
    }

    // Find max bin count for normalization.
    uint32_t maxCount = 0;
    for (uint32_t v : m_histogram.bins) maxCount = std::max(maxCount, v);
    if (maxCount == 0) maxCount = 1;

    const int numBins = static_cast<int>(m_histogram.bins.size());
    const QColor fillColor(
        static_cast<int>(std::round(m_colorR * 255.0f)),
        static_cast<int>(std::round(m_colorG * 255.0f)),
        static_cast<int>(std::round(m_colorB * 255.0f)),
        102);  // ~40% alpha
    const QColor strokeColor(
        static_cast<int>(std::round(m_colorR * 255.0f)),
        static_cast<int>(std::round(m_colorG * 255.0f)),
        static_cast<int>(std::round(m_colorB * 255.0f)));

    // Build histogram polygon.
    QPainterPath path;
    path.moveTo(0, h);
    for (int i = 0; i < numBins; ++i) {
        const double xd = static_cast<double>(i) / (numBins - 1) * (w - 1);
        double frac;
        if (m_logScale) {
            const double num = std::log(1.0 + static_cast<double>(m_histogram.bins[i]));
            const double den = std::log(1.0 + static_cast<double>(maxCount));
            frac = (den > 0.0) ? (num / den) : 0.0;
        } else {
            frac = static_cast<double>(m_histogram.bins[i]) / static_cast<double>(maxCount);
        }
        const double yd = h - 1 - frac * (h - 2);
        path.lineTo(xd, yd);
    }
    path.lineTo(w - 1, h);
    path.closeSubpath();

    p.fillPath(path, fillColor);
    p.setPen(QPen(strokeColor, 1.0));
    p.drawPath(path);

    // Clipped-region overlays (outside [displayMin, displayMax]).
    const int minX = valueToX(m_displayMin);
    const int maxX = valueToX(m_displayMax);
    p.fillRect(QRect(0, 0, minX, h), QColor(0, 0, 0, 64));
    p.fillRect(QRect(maxX + 1, 0, w - maxX - 1, h), QColor(0, 0, 0, 64));

    // Handle lines.
    QPen handlePen(QColor(0xFF, 0xCC, 0x33), kHandleVisualWidth);
    p.setPen(handlePen);
    p.drawLine(minX, 0, minX, h - 1);
    p.drawLine(maxX, 0, maxX, h - 1);
}

void ChannelHistogramView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_dragHandle = hitHandle(event->pos().x());
    if (m_dragHandle != Handle::None) {
        setCursor(Qt::SplitHCursor);
    }
}

void ChannelHistogramView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragHandle == Handle::None) {
        // Hover affordance only.
        setCursor(hitHandle(event->pos().x()) == Handle::None
                  ? Qt::ArrowCursor : Qt::SplitHCursor);
        return;
    }

    const double newVal = xToValue(event->pos().x());
    if (m_dragHandle == Handle::Min) {
        double clamped = clampToRange(newVal, m_histogram.rangeMin,
                                      m_displayMax - 1e-6);
        if (clamped >= m_displayMax) clamped = m_displayMax - 1e-6;
        m_displayMin = clamped;
    } else {
        double clamped = clampToRange(newVal, m_displayMin + 1e-6,
                                      m_histogram.rangeMax);
        if (clamped <= m_displayMin) clamped = m_displayMin + 1e-6;
        m_displayMax = clamped;
    }
    update();
    emit displayRangeChanged(m_displayMin, m_displayMax);
}

void ChannelHistogramView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_dragHandle != Handle::None) {
        m_dragHandle = Handle::None;
        setCursor(hitHandle(event->pos().x()) == Handle::None
                  ? Qt::ArrowCursor : Qt::SplitHCursor);
        emit displayRangeCommitted(m_displayMin, m_displayMax);
    }
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 3: Add the source to the UI target sources**

Open `src/ui/CMakeLists.txt`. Add `src/ChannelHistogramView.cpp` to the source list of the `slideio-viewer-ui` library, alongside the existing `src/ChannelMixerPanel.cpp`.

- [ ] **Step 4: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds; Qt MOC processes the new `Q_OBJECT` class automatically because `CMAKE_AUTOMOC` is on (see project root `CMakeLists.txt:32`).

- [ ] **Step 5: Commit**

```
git add src/ui/include/slideio/viewer/ui/ChannelHistogramView.h \
        src/ui/src/ChannelHistogramView.cpp \
        src/ui/CMakeLists.txt
git commit -m "Add ChannelHistogramView widget with draggable min/max handles"
```

---

## Task 8: `ChannelMixerPanel` — disclosure triangle and per-row expansion plumbing

**Files:**
- Modify: `src/ui/src/ChannelMixerPanel.cpp`

This task adds the visual disclosure affordance and expansion state, but does NOT yet add the histogram + min/max controls to the expanded section. That comes in Task 9.

- [ ] **Step 1: Extend `ChannelRow` and `Impl` with expansion state**

Open `src/ui/src/ChannelMixerPanel.cpp`. Find the `struct ChannelRow` inside `Impl` (lines 19–25). Replace with:

```cpp
struct ChannelRow
{
    QToolButton* disclosure = nullptr;
    QCheckBox*   checkBox = nullptr;
    QLabel*      nameLabel = nullptr;
    QPushButton* colorButton = nullptr;
    QSlider*     intensitySlider = nullptr;
    QWidget*     expandedContainer = nullptr;  // null until first expanded
    bool         expanded = false;
};
```

At the top of the file, add include:

```cpp
#include <QToolButton>
```

- [ ] **Step 2: Add a disclosure button to each row in `rebuildRows`**

In `rebuildRows()` (lines 66–127), at the start of the per-channel loop (right after `ChannelRow row;` and before `auto* hLayout = new QHBoxLayout();`), add:

```cpp
row.disclosure = new QToolButton(contentWidget);
row.disclosure->setArrowType(Qt::RightArrow);
row.disclosure->setAutoRaise(true);
row.disclosure->setFixedSize(12, 12);
row.disclosure->setStyleSheet("QToolButton { border: none; }");
QObject::connect(row.disclosure, &QToolButton::clicked, owner, [this, i]() {
    toggleRowExpanded(i);
});
```

Insert it as the first widget in `hLayout`:

```cpp
hLayout->addWidget(row.disclosure);
```

(immediately before `hLayout->addWidget(row.checkBox);`).

- [ ] **Step 3: Add `toggleRowExpanded` method**

The simplest correct approach is to mark the row as toggled and rebuild the row list (mirroring how `clearRows` already wholesale-rebuilds the content widget). Add this method to `Impl`, placed after `rebuildRows()`:

```cpp
void toggleRowExpanded(size_t index)
{
    if (index >= rows.size()) return;
    rows[index].expanded = !rows[index].expanded;
    rebuildRows();
}
```

For `rebuildRows()` to preserve expansion across the rebuild, it must read each row's `expanded` flag from the previous `rows` vector **before** `clearRows()` wipes it. Modify `rebuildRows()` to capture and restore:

```cpp
void rebuildRows()
{
    std::vector<bool> previousExpanded(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) previousExpanded[i] = rows[i].expanded;

    clearRows();
    rows.reserve(channels.size());

    for (size_t i = 0; i < channels.size(); ++i) {
        // ... existing row construction (with the disclosure button from Step 2) ...

        contentLayout->addLayout(hLayout);

        if (i < previousExpanded.size() && previousExpanded[i]) {
            row.expanded = true;
            row.disclosure->setArrowType(Qt::DownArrow);
            // Placeholder — Task 9 fills this in with the histogram view.
            auto* placeholder = new QLabel("(expanded)", contentWidget);
            placeholder->setStyleSheet("color:#666; margin-left: 24px;");
            row.expandedContainer = placeholder;
            contentLayout->addWidget(placeholder);
        }

        rows.push_back(row);
    }

    contentLayout->addStretch();
}
```

- [ ] **Step 4: Build and verify the disclosure triangle appears**

```
cmake --build build/build --config Release
```

Expected: build succeeds. Run the viewer, open a fluorescence slide, observe a small right-pointing triangle to the left of every channel row's checkbox. Click it — it should turn into a down-pointing arrow and a "(expanded)" placeholder text appears below the row. Click again to collapse.

- [ ] **Step 5: Commit**

```
git add src/ui/src/ChannelMixerPanel.cpp
git commit -m "Add per-row disclosure triangle and expansion state to ChannelMixerPanel"
```

---

## Task 9: Embed `ChannelHistogramView` + Min/Max textboxes in the expanded section

**Files:**
- Modify: `src/ui/src/ChannelMixerPanel.cpp`

- [ ] **Step 1: Add includes**

At the top of `src/ui/src/ChannelMixerPanel.cpp`, add:

```cpp
#include "slideio/viewer/ui/ChannelHistogramView.h"
#include <QDoubleValidator>
#include <QLineEdit>
#include <QFrame>
```

- [ ] **Step 2: Extend `ChannelRow` with handles to the new widgets**

Add these members to `ChannelRow` (created in Task 8):

```cpp
ChannelHistogramView* histogramView = nullptr;
QLineEdit*            minEdit = nullptr;
QLineEdit*            maxEdit = nullptr;
bool                  logScale = true;
```

- [ ] **Step 3: Replace the placeholder in `rebuildRows` with a real expanded container**

In the `if (i < previousExpanded.size() && previousExpanded[i])` branch (from Task 8 Step 3), replace the placeholder construction with a call to `buildExpandedContainer(i)`:

```cpp
if (i < previousExpanded.size() && previousExpanded[i]) {
    row.expanded = true;
    row.disclosure->setArrowType(Qt::DownArrow);
    row.expandedContainer = buildExpandedContainer(i, row);
    contentLayout->addWidget(row.expandedContainer);
}
```

The signature passes `row` by reference so the helper can populate `histogramView`, `minEdit`, `maxEdit`.

- [ ] **Step 4: Implement `buildExpandedContainer`**

Add this method to `Impl`, placed after `rebuildRows()`:

```cpp
QWidget* buildExpandedContainer(size_t index, ChannelRow& row)
{
    if (index >= channels.size()) return nullptr;
    const auto& ch = channels[index];

    auto* frame = new QFrame(contentWidget);
    frame->setStyleSheet(
        "QFrame { background-color: #252525; margin-left: 24px; padding: 6px; }");
    auto* vLayout = new QVBoxLayout(frame);
    vLayout->setContentsMargins(6, 6, 6, 6);
    vLayout->setSpacing(4);

    // --- Histogram view ---
    row.histogramView = new ChannelHistogramView(frame);
    row.histogramView->setHistogram(ch.histogram);
    row.histogramView->setChannelColor(ch.colorR, ch.colorG, ch.colorB);
    row.histogramView->setDisplayRange(ch.displayRange.displayMin,
                                       ch.displayRange.displayMax);
    row.histogramView->setLogScale(row.logScale);
    vLayout->addWidget(row.histogramView);

    // --- Min/Max textbox row ---
    auto* editRow = new QHBoxLayout();
    editRow->setSpacing(4);

    auto* minLabel = new QLabel("Min", frame);
    minLabel->setStyleSheet("color: #CCC; font-size: 11px;");
    editRow->addWidget(minLabel);

    row.minEdit = new QLineEdit(frame);
    row.minEdit->setValidator(new QDoubleValidator(row.minEdit));
    row.minEdit->setText(QString::number(ch.displayRange.displayMin));
    row.minEdit->setFixedWidth(64);
    row.minEdit->setStyleSheet(
        "QLineEdit { background:#333; color:#CCC; border:1px solid #555; padding:1px 4px; }");
    editRow->addWidget(row.minEdit);

    editRow->addStretch();

    auto* maxLabel = new QLabel("Max", frame);
    maxLabel->setStyleSheet("color: #CCC; font-size: 11px;");
    editRow->addWidget(maxLabel);

    row.maxEdit = new QLineEdit(frame);
    row.maxEdit->setValidator(new QDoubleValidator(row.maxEdit));
    row.maxEdit->setText(QString::number(ch.displayRange.displayMax));
    row.maxEdit->setFixedWidth(64);
    row.maxEdit->setStyleSheet(
        "QLineEdit { background:#333; color:#CCC; border:1px solid #555; padding:1px 4px; }");
    editRow->addWidget(row.maxEdit);

    vLayout->addLayout(editRow);

    // --- Wire histogram drag -> textboxes (live) ---
    QObject::connect(row.histogramView, &ChannelHistogramView::displayRangeChanged,
                     owner, [this, index](double minV, double maxV) {
        if (index >= rows.size()) return;
        if (rows[index].minEdit) rows[index].minEdit->setText(QString::number(minV));
        if (rows[index].maxEdit) rows[index].maxEdit->setText(QString::number(maxV));
    });

    // --- Wire histogram release -> commit (emits channelSettingsChanged) ---
    QObject::connect(row.histogramView, &ChannelHistogramView::displayRangeCommitted,
                     owner, [this, index](double minV, double maxV) {
        commitDisplayRange(index, minV, maxV);
    });

    // --- Wire textbox edits ---
    QObject::connect(row.minEdit, &QLineEdit::editingFinished, owner, [this, index]() {
        commitMinFromEdit(index);
    });
    QObject::connect(row.maxEdit, &QLineEdit::editingFinished, owner, [this, index]() {
        commitMaxFromEdit(index);
    });

    return frame;
}

void commitDisplayRange(size_t index, double minV, double maxV)
{
    if (index >= channels.size()) return;
    channels[index].displayRange.displayMin = minV;
    channels[index].displayRange.displayMax = maxV;
    channels[index].userOverrideRange = true;
    if (rows[index].minEdit) rows[index].minEdit->setText(QString::number(minV));
    if (rows[index].maxEdit) rows[index].maxEdit->setText(QString::number(maxV));
    emit owner->channelSettingsChanged(channels);
}

void commitMinFromEdit(size_t index)
{
    if (index >= channels.size() || !rows[index].minEdit) return;
    bool ok = false;
    double v = rows[index].minEdit->text().toDouble(&ok);
    if (!ok) {
        rows[index].minEdit->setText(QString::number(channels[index].displayRange.displayMin));
        return;
    }
    const double eps = epsilonFor(channels[index].dataType);
    if (v >= channels[index].displayRange.displayMax) {
        v = channels[index].displayRange.displayMax - eps;
    }
    channels[index].displayRange.displayMin = v;
    channels[index].userOverrideRange = true;
    rows[index].minEdit->setText(QString::number(v));
    if (rows[index].histogramView) {
        rows[index].histogramView->setDisplayRange(v, channels[index].displayRange.displayMax);
    }
    emit owner->channelSettingsChanged(channels);
}

void commitMaxFromEdit(size_t index)
{
    if (index >= channels.size() || !rows[index].maxEdit) return;
    bool ok = false;
    double v = rows[index].maxEdit->text().toDouble(&ok);
    if (!ok) {
        rows[index].maxEdit->setText(QString::number(channels[index].displayRange.displayMax));
        return;
    }
    const double eps = epsilonFor(channels[index].dataType);
    if (v <= channels[index].displayRange.displayMin) {
        v = channels[index].displayRange.displayMin + eps;
    }
    channels[index].displayRange.displayMax = v;
    channels[index].userOverrideRange = true;
    rows[index].maxEdit->setText(QString::number(v));
    if (rows[index].histogramView) {
        rows[index].histogramView->setDisplayRange(channels[index].displayRange.displayMin, v);
    }
    emit owner->channelSettingsChanged(channels);
}

static double epsilonFor(core::DataType dt)
{
    switch (dt) {
        case core::DataType::Float32:
        case core::DataType::Float64:
            return 1e-6;
        default:
            return 1.0;  // integer types snap by 1
    }
}
```

(`epsilonFor` is `static` because it has no dependency on `Impl` state.)

- [ ] **Step 5: Build and smoke-test**

```
cmake --build build/build --config Release
```

Run the viewer, open a fluorescence slide, expand a channel. Verify:
- Histogram appears with the channel color.
- Min/Max textboxes show the current displayMin/displayMax.
- Dragging a yellow handle on the histogram updates the textbox live.
- Releasing the handle triggers a viewport repaint (the image changes brightness).
- Typing a value into Min or Max and pressing Enter (or Tab/click-away) updates the viewport.
- Typing Min >= Max snaps Min to Max - 1 (or Max - 1e-6 for Float32).

- [ ] **Step 6: Commit**

```
git add src/ui/src/ChannelMixerPanel.cpp
git commit -m "Embed ChannelHistogramView + min/max textboxes in expanded channel row"
```

---

## Task 10: Auto / Reset / Log-Lin buttons in the expanded section

**Files:**
- Modify: `src/ui/src/ChannelMixerPanel.cpp`

- [ ] **Step 1: Add `QPushButton` includes if not already present**

`QPushButton` is already included (line 8 of the existing file). No new includes needed.

- [ ] **Step 2: Add three buttons to the `buildExpandedContainer` edit row**

Inside `buildExpandedContainer`, after the existing `editRow` (which holds Min label + min edit + stretch + Max label + max edit), append:

```cpp
    auto* autoBtn = new QPushButton("Auto", frame);
    autoBtn->setEnabled(ch.autoDisplayRange.autoDetected);
    autoBtn->setStyleSheet(
        "QPushButton { background:#444; color:#CCC; border:1px solid #666; padding:2px 8px; }"
        "QPushButton:disabled { color:#666; }");
    editRow->addWidget(autoBtn);
    QObject::connect(autoBtn, &QPushButton::clicked, owner, [this, index]() {
        applyAutoRange(index);
    });

    auto* resetBtn = new QPushButton("Reset", frame);
    resetBtn->setStyleSheet(
        "QPushButton { background:#444; color:#CCC; border:1px solid #666; padding:2px 8px; }");
    editRow->addWidget(resetBtn);
    QObject::connect(resetBtn, &QPushButton::clicked, owner, [this, index]() {
        applyResetRange(index);
    });

    auto* logBtn = new QToolButton(frame);
    logBtn->setText(row.logScale ? "Log" : "Lin");
    logBtn->setCheckable(true);
    logBtn->setChecked(row.logScale);
    logBtn->setStyleSheet(
        "QToolButton { background:#444; color:#CCC; border:1px solid #666; padding:2px 8px; }"
        "QToolButton:checked { background:#555; }");
    editRow->addWidget(logBtn);
    QObject::connect(logBtn, &QToolButton::toggled, owner, [this, index, logBtn](bool checked) {
        if (index >= rows.size()) return;
        rows[index].logScale = checked;
        logBtn->setText(checked ? "Log" : "Lin");
        if (rows[index].histogramView) {
            rows[index].histogramView->setLogScale(checked);
        }
    });
```

- [ ] **Step 3: Implement `applyAutoRange` and `applyResetRange` on `Impl`**

Add these methods after `commitMaxFromEdit`:

```cpp
void applyAutoRange(size_t index)
{
    if (index >= channels.size()) return;
    auto& ch = channels[index];
    if (!ch.autoDisplayRange.autoDetected) return;
    ch.displayRange = ch.autoDisplayRange;
    ch.userOverrideRange = false;
    syncRowControls(index);
    emit owner->channelSettingsChanged(channels);
}

void applyResetRange(size_t index)
{
    if (index >= channels.size()) return;
    auto& ch = channels[index];
    const auto [rMin, rMax] = dataTypeFullRange(ch.dataType);
    ch.displayRange.displayMin = rMin;
    ch.displayRange.displayMax = rMax;
    ch.displayRange.autoDetected = false;
    ch.userOverrideRange = true;
    syncRowControls(index);
    emit owner->channelSettingsChanged(channels);
}

void syncRowControls(size_t index)
{
    if (index >= rows.size()) return;
    auto& row = rows[index];
    const auto& ch = channels[index];
    if (row.minEdit) row.minEdit->setText(QString::number(ch.displayRange.displayMin));
    if (row.maxEdit) row.maxEdit->setText(QString::number(ch.displayRange.displayMax));
    if (row.histogramView) {
        row.histogramView->setDisplayRange(ch.displayRange.displayMin,
                                           ch.displayRange.displayMax);
    }
}

static std::pair<double, double> dataTypeFullRange(core::DataType dt)
{
    switch (dt) {
        case core::DataType::Byte:    return {0.0, 255.0};
        case core::DataType::Int8:    return {-128.0, 127.0};
        case core::DataType::UInt16:  return {0.0, 65535.0};
        case core::DataType::Int16:   return {0.0, 32767.0};
        case core::DataType::UInt32:  return {0.0, 4294967295.0};
        case core::DataType::Int32:   return {0.0, 2147483647.0};
        case core::DataType::Float32: return {0.0, 1.0};
        case core::DataType::Float64: return {0.0, 1.0};
        default:                      return {0.0, 255.0};
    }
}
```

- [ ] **Step 4: Build and smoke-test**

```
cmake --build build/build --config Release
```

Run the viewer:
- Expand a channel. Drag handles to make the image look wrong. Click **Auto** — the handles should snap back to the autodetected positions and the image returns to its post-open look.
- Click **Reset** — the handles should jump to the data-type extremes (0 and 65535 for UInt16). The image will likely become very dim (since the range now covers the whole type).
- Toggle **Log/Lin** — the histogram shape changes (log mode shows the long tail; linear collapses it).
- Open a slide where autodetect failed (rare; can synthesize by opening a slide and checking logs for `autoDetected=false`). The Auto button should be greyed out for affected channels.

- [ ] **Step 5: Commit**

```
git add src/ui/src/ChannelMixerPanel.cpp
git commit -m "Add Auto, Reset, and Log/Lin buttons to expanded channel row"
```

---

## Task 11: Master "All channels" tri-state checkbox + "N visible" label

**Files:**
- Modify: `src/ui/src/ChannelMixerPanel.cpp`

- [ ] **Step 1: Add header member fields**

Inside `Impl`, near the other members (`scrollArea`, `contentWidget`, `contentLayout`), add:

```cpp
QCheckBox* masterCheckBox = nullptr;
QLabel*    visibleCountLabel = nullptr;
std::optional<std::vector<bool>> preToggleVisibility;
```

(These pointers point into the same `contentWidget` that `clearRows` destroys wholesale, so no manual cleanup is needed — they get rebuilt in the next `rebuildRows`.)

At the top of the file, add:

```cpp
#include <optional>
```

- [ ] **Step 2: Build the master header in `buildUi`**

Open `buildUi()` (currently around line 35). Before `contentWidget = new QWidget(scrollArea);`, add nothing — the header lives inside `contentWidget` so it scrolls with the channel list. After `contentLayout = new QVBoxLayout(contentWidget);` and before `contentLayout->setContentsMargins(...)`, **do not change anything**. The header is created in `rebuildRows`, since it depends on channel state.

- [ ] **Step 3: Build the master header at the top of `rebuildRows`**

Modify `rebuildRows()` so that immediately after `clearRows()`, but before the per-channel loop, it constructs the header. Insert this block right after `rows.reserve(channels.size());`:

```cpp
// --- Master toggle header ---
auto* headerLayout = new QHBoxLayout();
headerLayout->setContentsMargins(0, 0, 0, 4);
headerLayout->setSpacing(4);

masterCheckBox = new QCheckBox(contentWidget);
masterCheckBox->setTristate(true);
masterCheckBox->setStyleSheet("QCheckBox { color: #CCC; font-weight: bold; }");
masterCheckBox->setText("All channels");
QObject::connect(masterCheckBox, &QCheckBox::clicked, owner, [this](bool /*checked*/) {
    onMasterClicked();
});
headerLayout->addWidget(masterCheckBox);
headerLayout->addStretch();

visibleCountLabel = new QLabel(contentWidget);
visibleCountLabel->setStyleSheet("color:#888; font-size:11px;");
headerLayout->addWidget(visibleCountLabel);

contentLayout->addLayout(headerLayout);

// Separator
auto* sep = new QFrame(contentWidget);
sep->setFrameShape(QFrame::HLine);
sep->setStyleSheet("color:#444;");
contentLayout->addWidget(sep);

refreshMasterState();
```

- [ ] **Step 4: Implement the master toggle logic**

Add to `Impl`, placed after `applyResetRange`:

```cpp
void onMasterClicked()
{
    // The Qt click handler fires before the widget's check-state is updated
    // by Qt's automatic tri-state toggle. So we read the PRE-click state and
    // decide the action ourselves.
    Qt::CheckState pre = currentMasterState();
    if (pre == Qt::Checked || pre == Qt::PartiallyChecked) {
        // -> all-off, snapshot first
        std::vector<bool> snapshot(channels.size());
        for (size_t i = 0; i < channels.size(); ++i) snapshot[i] = channels[i].visible;
        preToggleVisibility = std::move(snapshot);
        for (auto& ch : channels) ch.visible = false;
    } else {
        // pre == Unchecked
        if (preToggleVisibility.has_value() &&
            preToggleVisibility->size() == channels.size()) {
            for (size_t i = 0; i < channels.size(); ++i) {
                channels[i].visible = (*preToggleVisibility)[i];
            }
            preToggleVisibility.reset();
        } else {
            for (auto& ch : channels) ch.visible = true;
        }
    }

    // Reflect into per-row checkboxes WITHOUT re-emitting their toggled signals.
    for (size_t i = 0; i < rows.size() && i < channels.size(); ++i) {
        if (rows[i].checkBox) {
            QSignalBlocker block(rows[i].checkBox);
            rows[i].checkBox->setChecked(channels[i].visible);
        }
    }

    refreshMasterState();
    emit owner->channelSettingsChanged(channels);
}

Qt::CheckState currentMasterState() const
{
    if (channels.empty()) return Qt::Unchecked;
    size_t onCount = 0;
    for (const auto& ch : channels) if (ch.visible) ++onCount;
    if (onCount == 0) return Qt::Unchecked;
    if (onCount == channels.size()) return Qt::Checked;
    return Qt::PartiallyChecked;
}

void refreshMasterState()
{
    if (!masterCheckBox || !visibleCountLabel) return;
    QSignalBlocker block(masterCheckBox);
    masterCheckBox->setCheckState(currentMasterState());

    size_t visibleCount = 0;
    for (const auto& ch : channels) if (ch.visible) ++visibleCount;
    visibleCountLabel->setText(QString("%1 visible").arg(visibleCount));
}
```

- [ ] **Step 5: Refresh master state when individual checkboxes toggle**

In `rebuildRows()`, find the existing connect for the per-row visibility checkbox:

```cpp
QObject::connect(row.checkBox, &QCheckBox::toggled, owner, [this, i](bool checked) {
    if (i < channels.size()) {
        channels[i].visible = checked;
        emit owner->channelSettingsChanged(channels);
    }
});
```

Replace with:

```cpp
QObject::connect(row.checkBox, &QCheckBox::toggled, owner, [this, i](bool checked) {
    if (i < channels.size()) {
        channels[i].visible = checked;
        refreshMasterState();
        emit owner->channelSettingsChanged(channels);
    }
});
```

- [ ] **Step 6: Clear `preToggleVisibility` whenever the channel list is rebuilt**

In `setChannels`, before `m_impl->rebuildRows();`, add:

```cpp
m_impl->preToggleVisibility.reset();
```

In `clearChannels`, before `m_impl->clearRows();`, add the same line.

- [ ] **Step 7: Build and smoke-test**

```
cmake --build build/build --config Release
```

Run the viewer, open a fluorescence slide with ≥3 channels:
- All channels initially visible → master shows ☑ Checked, label shows "N visible" with N = channel count.
- Uncheck one row → master flips to PartiallyChecked, count decreases.
- Click master once → all channels off, label "0 visible", master Unchecked.
- Click master again → channels restore to the pre-toggle state (the one row you unchecked stays unchecked).
- Click master once more → all-off again (snapshot updated).
- Reload the slide (open a different one) and verify the snapshot was cleared (no stale restore).

- [ ] **Step 8: Commit**

```
git add src/ui/src/ChannelMixerPanel.cpp
git commit -m "Add tri-state master 'All channels' toggle with restore-previous"
```

---

## Task 12: Manual smoke-test checklist (no code changes)

**Files:**
- (none — verification only)

This task is a final integration check. Document the outcomes in the commit message if anything noteworthy comes up.

- [ ] **Step 1: Build clean**

```
cmake --build build/build --config Release
```

- [ ] **Step 2: Run the core unit tests**

```
build/build/tests/Release/slideio-viewer-core-tests.exe --reporter console
```

Expected: all tests pass (no regressions in the 4 existing test files + the 6 new histogram cases).

- [ ] **Step 3: Open a 4-channel fluorescence Z-stack (e.g., a VSI file)**

- Expand each channel in turn. Verify histograms render with each channel's color.
- Drag handles on the FITC channel. Confirm: textboxes update live, viewport updates on release only, image brightness visibly changes.
- Type a Min value into the DAPI channel that's greater than its Max. Confirm snap to `Max - 1`.
- Click Auto on a channel. Confirm: handles return to autodetected positions, image returns to its open-time look.
- Click Reset on a UInt16 channel. Confirm: Min = 0, Max = 65535 in the textboxes, image becomes very dim.
- Toggle Log/Lin on a histogram with a heavy zero-spike. Confirm shape change.

- [ ] **Step 4: Open a 3-channel brightfield H&E slide**

- Expand the R channel. Confirm histogram appears (this is the new behavior).
- Drag the Max handle leftward. Confirm: image brightens / saturates as expected — verifies the renderer gating change is wired for brightfield.
- Click Reset on G. Confirm: G channel range = {0, 255}.

- [ ] **Step 5: Test the master toggle**

- All channels visible → master ☑.
- Uncheck FITC manually → master becomes ▪ (partial), label "3 visible".
- Click master → all off, label "0 visible".
- Click master → restore (FITC stays off).
- Reload the slide → master state cycles cleanly from a fresh start.

- [ ] **Step 6: Test single-channel slide**

- Open a single-channel fluorescence slide (UInt16). Verify the panel shows the master row, one channel row with disclosure + histogram, all interactions work.

- [ ] **Step 7: Final commit**

If anything required fixes during smoke-testing, those should already be committed individually. This task itself produces no commit.

---

## Spec coverage summary

| Spec section | Covered by |
|---|---|
| §1.1 `ChannelHistogram` struct | Task 1 |
| §1.2 `ChannelInfo` new fields | Task 1 |
| §2.1 Block read parameters, `kMaxThumbDim` bump | Task 5 |
| §2.2 Binning | Tasks 2, 3, 4 |
| §2.3 `autoDisplayRange` capture | Task 5 (Step 6) |
| §3.1 Master tri-state toggle | Task 11 |
| §3.2 Disclosure triangle | Task 8 |
| §3.3 Expanded section + min/max + Auto/Reset/Log-Lin | Tasks 9, 10 |
| §3.4 Existing signal reuse + write-once histogram invariant | Tasks 9, 10, 11 (use existing signal); Task 6 verifies the struct-assignment round-trip preserves the new fields |
| §4 `ChannelHistogramView` | Task 7 |
| §5 Renderer gating | Task 6 |
| §6 Edge cases | Spread across Tasks 5–11 (each edge case handled inline) |
| §7.1 Core unit tests | Tasks 3, 4 |
| §7.2 Widget tests | **Deviation from spec.** No automated widget tests. The project does not currently have a UI test target (`tests/ui/`); standing one up is a meaningful CMake/Catch2/QApplication scope expansion that is out of proportion with this feature. The previous UI-pane feature (metadata dock) followed the same precedent. Covered instead by the manual checklist in §7.3 / Task 12. |
| §7.3 Manual smoke checklist | Task 12 |
