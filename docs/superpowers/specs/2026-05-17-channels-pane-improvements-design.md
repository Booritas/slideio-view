# Channels pane improvements — design

**Date:** 2026-05-17
**Status:** Approved (proceeding to implementation plan)

## Goal

Extend the existing `ChannelMixerPanel` (right-dock "Channels" panel) so the user can:

1. Toggle visibility of all channels at once (master "All channels" tri-state checkbox).
2. See a histogram of each channel's pixel values.
3. Interactively edit each channel's display range (min/max), with the change applied live to the viewport on release.

The histogram is computed once per slide open from a fixed-size downsampled block read via `slideio::Scene::readBlock`. The renderer wiring is adjusted so that user-edited per-channel ranges apply to both fluorescence and brightfield (RGB) slides uniformly.

## Non-goals

- **No persistence across slide reopens.** All channel settings (visibility, color, intensity, manual min/max) reset to autodetect on each open. No sidecar files, no global defaults.
- **No live histogram refresh** when the user changes Z or T slice. Histogram remains the slide-open snapshot (middle slice). A future "refresh" affordance can be added if asked for.
- **No whole-slide histograms.** The 1000×1000 downsampled snapshot is sufficient for visual intensity distribution; a full-scene pass is not justified for the use case.
- **No per-bin tooltips, no cumulative histogram overlay, no statistics box.** The histogram is a visual aid for picking min/max, not a quantitative analysis tool.

## Architecture

The viewer enforces strict downward layering: `core` is pure C++ with no Qt or SlideIO; `ui` does not link SlideIO directly. The histogram type lives in `core`; its computation lives in `ui` (alongside the existing autodetect, which already runs in `ViewportWidget`).

```
slideio::Scene::readBlock(...)        (SlideIO library, infra-only)
        │
        ▼  one downsampled block at slide open (≤1000px on the larger side, middle Z slice)
ViewportWidget::computeChannelDisplayRanges
        │  ┌─ autodetect min/max (existing)
        │  └─ NEW: bin pixels into per-channel histograms
        ▼
core::ChannelInfo { histogram, autoDisplayRange, displayRange, userOverrideRange }
        │
        ▼  carried inside core::SlideInfo and emitted to UI via slideOpened
ui::ChannelMixerPanel
        │
        ├─ tri-state "All channels" master toggle
        ├─ existing per-row controls (checkbox, name, color, intensity)
        ├─ NEW: disclosure triangle (per-row expand)
        └─ NEW (expanded section per row):
             ├─ ChannelHistogramView (custom QWidget, draggable handles)
             ├─ Min/Max textboxes
             ├─ Auto / Reset / Log-Lin buttons
             ▼
        channelSettingsChanged signal → MainWindow → ViewportWidget::setChannelSettings
        │
        ▼  renderer uses chInfo.userOverrideRange || chInfo.displayRange.autoDetected
```

## 1. Data model

### 1.1 New type — `core::ChannelHistogram`

In `src/core/include/slideio/viewer/core/Types.h`:

```cpp
struct ChannelHistogram
{
    std::vector<uint32_t> bins;    // size = kNumBins (256)
    double rangeMin = 0.0;         // pixel value that maps to bins[0]
    double rangeMax = 0.0;         // pixel value that maps to bins.back()
    uint64_t totalSamples = 0;     // sum of bins[] — used to guard log-axis scaling
    bool valid = false;            // false if computation failed/skipped
};
```

`kNumBins` is `256` (`constexpr int` in `Types.h`). Byte data gets exactly one bin per code; higher datatypes are quantized to 256 bins. There is no UI to change this.

`rangeMin`/`rangeMax` are set from the channel's data-type full range (see §2), so handle pixel-to-value mapping matches the renderer's `toSamplerRange`.

### 1.2 Extend `core::ChannelInfo`

```cpp
struct ChannelInfo
{
    std::string name;
    DataType dataType = DataType::None;
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float intensity = 1.0f;
    bool visible = true;
    DisplayRange displayRange;                // current effective range (may be user-edited)
    DisplayRange autoDisplayRange;            // NEW: frozen snapshot of autodetect output
    ChannelHistogram histogram;               // NEW: computed once at slide open
    bool userOverrideRange = false;           // NEW: set true when user edits min/max or clicks Reset
};
```

**Rationale for the new fields:**

- `autoDisplayRange` — snapshot of what the autodetect pass produced at slide open. The "Auto" button restores `displayRange` from this snapshot and clears `userOverrideRange`. Surviving user edits means we never need to re-run autodetect (which would require re-reading pixels).
- `userOverrideRange` — decouples "use per-channel range" from `displayRange.autoDetected`. Today the renderer at `ViewportWidget.cpp:2531` uses `chInfo.displayRange.autoDetected` as the gate; this conflates "autodetect succeeded" with "should this range be used at all". With the new flag, the renderer uses the per-channel range whenever EITHER autodetect succeeded OR the user has set a value. See §5.

## 2. Histogram computation

Folded into the existing autodetect routine in `ViewportWidget.cpp::computeChannelDisplayRanges` (current body around line 663). One block read, two passes (autodetect min/max, then histogram bins).

### 2.1 Block read parameters

- **Bump `kMaxThumbDim` from 800 to 1000** to match the spec.
- The existing `readBlock(0, 0, slideInfo.width, slideInfo.height, thumbW, thumbH, preferredZ)` call (`ViewportWidget.cpp:767`) already does what we need: downsampled to ≤1000px on the larger side, at the middle Z slice (`preferredZ = numZ > 1 ? numZ / 2 : 0`, line 698). If the original largest side is ≤1000 px, `thumbW/thumbH` equal the native dimensions and the full image is read.
- For multi-T (time) stacks, the same midpoint-selection logic applies: `preferredT = numT > 1 ? numT / 2 : 0`. SlideIO's `readBlock` overloads accept a T parameter alongside Z (see `slideio::Scene::readBlock(rect, size, channels, z, t)`); the histogram pass passes `preferredT` explicitly. The existing autodetect path implicitly uses T=0 today — that path is **not** changed here, only the histogram pass uses `preferredT`.
- The block read currently fires only when the coarse-tile pass fails (gated by `!slideInfo.displayRange.autoDetected && blockUsable` at line 774). **Change:** always perform the `readBlock` and always run histograms from it. Autodetect continues to prefer coarse-tile results when those succeed; the block becomes the dedicated histogram source.

### 2.2 Binning

```cpp
namespace {
constexpr int kNumBins = 256;

void computeHistogramStrided(const uint8_t* data, size_t pixelCount,
                             int numChannels, int channelIndex,
                             core::DataType dt,
                             double rangeMin, double rangeMax,
                             core::ChannelHistogram& out);
}
```

- `rangeMin`/`rangeMax` come from the data type:
  - `Byte` → `{0, 255}`
  - `UInt16` → `{0, 65535}`
  - `Int16` → `{0, 32767}`  (matches `ViewportWidget.cpp:1028`)
  - `Float32` → `{0.0, 1.0}`  (matches `ViewportWidget.cpp:1029`)
- Bin index: `idx = clamp(floor((value - rangeMin) / (rangeMax - rangeMin) * kNumBins), 0, kNumBins-1)`.
- Strided over interleaved pixels (the buffer is `[ch0,ch1,...,chN-1, ch0,ch1,...]`), one channel per call. Mirrors the existing `computeMinMaxStrided` pattern used by autodetect.

After binning, `histogram.valid = (totalSamples > 0)`.

### 2.3 Where `autoDisplayRange` is set

At the end of the autodetect routine, after `slideInfo.channels[ch].displayRange` is finalized for each channel, copy:

```cpp
ch.autoDisplayRange = ch.displayRange;
```

`displayRange.autoDetected` is left set to whatever autodetect produced (true if it succeeded). `userOverrideRange` starts as `false`.

## 3. UI — `ChannelMixerPanel`

Existing file: `src/ui/src/ChannelMixerPanel.cpp` / `src/ui/include/slideio/viewer/ui/ChannelMixerPanel.h`. The PIMPL `Impl` struct is extended; no public API breakage.

### 3.1 Master toggle header

A new fixed header row at the top of `contentLayout`, above the channel rows:

```
[☑] All channels                              N visible
```

- `QCheckBox` with `setTristate(true)`.
- Tri-state visual is **derived from channel state**, not stored on the checkbox itself:
  - `Qt::Checked` when every channel's `visible == true`
  - `Qt::Unchecked` when every channel's `visible == false`
  - `Qt::PartiallyChecked` otherwise (the user has manually unticked some rows)
- The panel owns `std::optional<std::vector<bool>> m_preToggleVisibility`.
- Click behavior, by current visual state:
  - **From Checked or PartiallyChecked** → snapshot the current per-channel visibility into `m_preToggleVisibility`, then set all channels to `visible = false`. Visual becomes Unchecked.
  - **From Unchecked** → if `m_preToggleVisibility` holds a value, restore visibility from it and clear the snapshot. If empty (initial state with all channels off), set all channels to `visible = true`.
- Right-aligned `QLabel` shows `"{N} visible"` and updates on every visibility change.
- When the channel list is rebuilt (`setChannels`), `m_preToggleVisibility` is cleared.
- Individual-row checkbox edits do NOT touch `m_preToggleVisibility` — only the master toggle's all-off transition writes to it.

For single-channel slides, the tri-state still works (partial state is just unused). No special-case code path.

### 3.2 Per-row layout

Current row (left-to-right): `[☑] name [color] [intensity-slider]`.

New row: `[▸] [☑] name [color] [intensity-slider]`.

- Disclosure triangle is a 12px `QToolButton` with `Qt::NoArrow` and a custom paint (drawn as a triangle pointing right when collapsed, down when expanded). Clicking toggles `m_rows[i].expanded`.
- Expansion state is panel-local (`bool ChannelRow::expanded`), not persisted, not reflected in `core::ChannelInfo`.

### 3.3 Expanded section

When `m_rows[i].expanded == true`, the row's parent layout includes a sub-widget below it, indented by 24px from the left:

```
┌────────────────────────────────────────────────────┐
│  [histogram view 60px high, full row width]        │
│                                                    │
│  Min [____]  [─────────] Max [____]  Auto Reset Log│
└────────────────────────────────────────────────────┘
```

- **Histogram view** — `ChannelHistogramView` (§4), receives `chInfo.histogram` and the current `chInfo.displayRange`. Channel color from `chInfo.colorR/G/B`.
- **Min textbox / Max textbox** — `QLineEdit` with `QDoubleValidator`. Edited on `editingFinished`. If the user types `Min ≥ Max`, snap to `Max - epsilon` (epsilon = 1 for integer types, 1e-6 for float). No error popup.
- **Auto button** — restores `displayRange = autoDisplayRange`, sets `userOverrideRange = false`. If `autoDisplayRange.autoDetected == false` (autodetect didn't produce a result), the button is disabled.
- **Reset button** — sets `displayRange` to the full data-type range (`{0, 255}` / `{0, 65535}` / etc.), sets `userOverrideRange = true`. Available unconditionally.
- **Log/Lin button** — small `QToolButton` (or `QCheckBox`); toggles the histogram view's Y-axis scale. Per-row state (`m_rows[i].logScale`), defaults to `true`. Not reflected in `core::ChannelInfo` (purely a UI display preference).

If `chInfo.histogram.valid == false`, the histogram view is replaced with a 60px-tall `QLabel("Histogram unavailable")` centered. Min/Max controls and Auto/Reset remain functional (operate on `autoDisplayRange` and data-type range respectively).

### 3.4 Wiring to the existing model

Existing signal `channelSettingsChanged(const std::vector<core::ChannelInfo>&)` is reused — no new signals on the panel. When a min/max edit, button press, or master toggle is committed, the panel updates its local `channels` vector and emits the signal. `MainWindow` already forwards this to `ViewportWidget::setChannelSettings` (`MainWindow.cpp:388`).

**Invariant:** `ChannelInfo::histogram` and `ChannelInfo::autoDisplayRange` are write-once at slide open. The panel reads them but never modifies them; `setChannelSettings` round-trips them unchanged. If `setChannelSettings` ever receives a different histogram than what it produced for the current slide, the renderer simply ignores the difference (only `displayRange`, `userOverrideRange`, `visible`, `intensity`, `color*` are consumed for rendering). This makes the round-trip safe without needing immutable-by-construction enforcement.

## 4. `ChannelHistogramView` (new widget)

New files:
- `src/ui/include/slideio/viewer/ui/ChannelHistogramView.h`
- `src/ui/src/ChannelHistogramView.cpp`

Standalone `QWidget` subclass. No Qt Charts dependency.

### 4.1 Public API

```cpp
class ChannelHistogramView : public QWidget {
    Q_OBJECT
public:
    explicit ChannelHistogramView(QWidget* parent = nullptr);

    void setHistogram(const core::ChannelHistogram& h);
    void setDisplayRange(double min, double max);
    void setChannelColor(float r, float g, float b);
    void setLogScale(bool log);

signals:
    void displayRangeChanged(double min, double max);     // emitted live while dragging
    void displayRangeCommitted(double min, double max);   // emitted on mouse release

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize sizeHint() const override { return {200, 60}; }
};
```

### 4.2 Painting

- Background: `#252525` (matches panel expanded background).
- Bins: filled polygon (one point per bin top), channel color at 40% alpha; stroke at full alpha and 1px.
- Bin heights (per pixel column):
  - Linear: `count / maxCount * H`
  - Log: `log(1 + count) / log(1 + maxCount) * H`
  - `maxCount = *std::max_element(bins)`; if zero, the bin row is drawn as flat at the baseline.
- Min/max handle indicators: two vertical lines at the handle x-positions, color `#FFCC33`, width 1.5 px.
- Clipped region overlay: 25% black rectangle covering x < minHandle, and x > maxHandle.

### 4.3 Hit testing and dragging

- 8px-wide invisible hit zones centered on each handle. Cursor changes to `Qt::SplitHCursor` on hover.
- On `mousePressEvent` inside a hit zone, store which handle is grabbed (`m_dragHandle ∈ {None, Min, Max}`).
- On `mouseMoveEvent` while dragging:
  - Map x-pixel → pixel value: `value = lerp(h.rangeMin, h.rangeMax, x / width)`.
  - Clamp so `Min < Max` stays true; snap to `Max - epsilon` (or `Min + epsilon`) if the user drags past.
  - Update internal display-range fields, repaint, emit `displayRangeChanged(min, max)`.
- On `mouseReleaseEvent`, clear `m_dragHandle` and emit `displayRangeCommitted(min, max)`.
- Clicks outside the hit zones are ignored. No whole-track click-to-jump.

### 4.4 Two-signal pattern

`displayRangeChanged` updates only the textboxes (cheap UI work — the panel does NOT propagate to the renderer).
`displayRangeCommitted` triggers the full `channelSettingsChanged` emission and re-uploads shader uniforms.

This is so a fast drag doesn't re-upload uniforms at 60+ Hz.

## 5. Renderer wiring change

Single one-line change at `ViewportWidget.cpp:2531`:

```cpp
// Before
const bool useChannelRange = chInfo.displayRange.autoDetected;

// After
const bool useChannelRange = chInfo.userOverrideRange || chInfo.displayRange.autoDetected;
```

Plus, in `setChannelSettings(...)` (the existing slot at `ViewportWidget.cpp:2136`), when applying incoming channel settings, the incoming `userOverrideRange` flag is copied as-is.

**Consequence for brightfield.** The existing autodetect pass at `ViewportWidget.cpp:741` already runs `if (numCh > 1)` — so RGB brightfield slides already produce per-channel `displayRange` with `autoDetected = true`. The single-line gating change above is what activates them in the renderer; no additional brightfield-specific code is required. The single-channel-brightfield fast path (`ViewportWidget.cpp:2375`) is unchanged.

## 6. Edge cases

| Case | Behavior |
|---|---|
| `readBlock` fails or returns empty | `histogram.valid = false` for all channels. UI shows "Histogram unavailable" placeholder; Min/Max textboxes and Auto/Reset still work. |
| All-zero or single-bin spike histogram | Paint draws a flat polygon (or near-flat). Log scaling guards against `log(0)` via `log(1 + count)`. |
| Single-channel slide | Master tri-state still works (partial state unused). Disclosure + histogram + min/max behave identically to multi-channel. |
| User types Min ≥ Max in textbox | On `editingFinished`, snap to `Max - epsilon` (epsilon = 1 for integer types, 1e-6 for float). No error popup. |
| Float32 with negative or > 1.0 values | Histogram `rangeMin/rangeMax = {0, 1}`. Out-of-range pixels clamp to edge bins. Handle drag still operates over `[0, 1]`. |
| Scene change (multi-scene file) | `setChannels(...)` rebuilds rows; new histograms read from the new scene. No special handling. |
| Z/T slice change after slide open | Histogram does NOT recompute. It remains the middle-slice snapshot. |
| Fast drag | `displayRangeChanged` updates textboxes only; only `displayRangeCommitted` (on release) triggers shader re-upload. No throttling. |
| `autoDisplayRange.autoDetected == false` (autodetect failed) | "Auto" button is disabled; "Reset" remains usable as the recovery path. |

## 7. Testing

### 7.1 Core unit tests (`tests/core/`)

- `computeChannelHistogram` over synthetic buffers for each `DataType` (Byte, UInt16, Int16, Float32):
  - Sum of bins equals pixel count.
  - Bin indices for known values are correct (e.g., UInt16 value 32767 → bin 127 for 256 bins).
  - All-zero buffer produces `bins[0] == pixelCount`, all other bins zero.
  - Empty buffer produces `valid == false`.
- `ChannelInfo` invariants:
  - Setting `userOverrideRange = true` is preserved through copy and move.
  - `autoDisplayRange` is independent of `displayRange` after edits.

### 7.2 Widget tests (`tests/ui/`)

- `ChannelHistogramView`:
  - Synthesize a histogram, simulate `mousePressEvent` on the Min-handle hit zone, `mouseMoveEvent` across the widget, `mouseReleaseEvent`. Verify:
    - `displayRangeChanged` fires once per `mouseMoveEvent`.
    - `displayRangeCommitted` fires exactly once on release.
    - Final min equals the lerped pixel value of the release x position.
  - Drag Min handle past Max: verify min snaps to `max - epsilon`.
- `ChannelMixerPanel`:
  - Tri-state master toggle: starting from all-on, click → all-off (visibility snapshot stored), click → restore-previous, click → all-off again. Verify `m_preToggleVisibility` semantics.
  - Disclosure triangle: expand row 1, leave row 0 collapsed. Verify row 1's expanded section is created and row 0's is not.
  - Auto button: set `displayRange` to arbitrary values, click Auto. Verify `displayRange == autoDisplayRange` and `userOverrideRange == false`.
  - Reset button on UInt16 channel: click Reset. Verify `displayRange == {0, 65535}` and `userOverrideRange == true`.

### 7.3 Manual smoke checklist (in spec, no automation)

- Open a fluorescence VSI Z-stack: confirm histograms appear, drag handles, viewport updates on release.
- Open a 3-channel RGB brightfield slide: confirm histograms appear for R/G/B, drag handles, brightness change is visible.
- Verify master tri-state cycle with a 4-channel fluorescence slide.

No end-to-end rendering tests — the existing project does not have that infrastructure and this change does not justify adding it.

## 8. Implementation order (suggested)

1. Core types — `ChannelHistogram`, `ChannelInfo` extensions (§1).
2. Histogram computation in `computeChannelDisplayRanges` (§2). Bump `kMaxThumbDim` and always run the `readBlock`.
3. Renderer gating change (§5). At this point, no UI changes yet — verify brightfield slides still render correctly via existing tests.
4. `ChannelHistogramView` widget (§4) with unit tests.
5. `ChannelMixerPanel` master tri-state + disclosure + expanded section (§3).
6. Wire Auto/Reset/Log-Lin buttons (§3.3).
7. Run manual smoke tests.

Each step lands as its own commit on `main` per the project's workflow.
