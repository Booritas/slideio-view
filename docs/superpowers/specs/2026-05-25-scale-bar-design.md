# Scale bar — design

**Date:** 2026-05-25
**Status:** Approved (proceeding to implementation plan)

## Goal

Add a graphical scale bar to the existing zoom-indicator overlay so the user can read the physical size of features in the viewport at a glance.

The bar:

- Sits **above** the magnification slider inside the existing `ZoomIndicatorWidget` overlay (the same widget that displays magnification, percentage, and the zoom slider).
- Has approximately the same length as the slider groove.
- Is drawn as a horizontal line with short vertical end caps, with a label centered below the bar showing the bar's physical length in micrometers (e.g. `100 µm`, `5000 µm`).
- Auto-picks a "nice" round length (1, 2, 5 series) so the label is always easy to read.
- Is shown **only when** the slide reports a valid pixel resolution (`SlideInfo::resolutionX > 0`).

## Non-goals

- **No mm/cm units.** The label is always in micrometers (µm), even when the bar represents 100 000 µm. A future preference toggle can be added if asked for.
- **No vertical scale bar.** Only the horizontal bar is implemented; horizontal/vertical pixel sizes are equal in every WSI format the viewer currently supports.
- **No user-configurable length.** The nice-length is chosen automatically; users do not pick a preferred length.
- **No anisotropy handling.** If `resolutionX != resolutionY` (rare), `resolutionX` is used and the difference is ignored.
- **No styling preferences.** The bar uses the same light-grey-on-dark palette as the slider; no theme/color settings.

## Architecture

The viewer enforces strict downward layering: `core` is pure C++ (no Qt, no SlideIO); `ui` consumes `core` types and adds widgets. The nice-length algorithm is a pure function placed in `core` so it can be unit-tested independently of Qt. The widget itself lives in `ui`.

```
SlideInfo::resolutionX  (meters/pixel native, from SlideIOAdapter)
        │
        ▼  passed once on slideOpened
ZoomIndicatorWidget::setResolution(metersPerPixel)
        │  stores m_metersPerPixel
        │
        ▼  recomputed on every setZoomLevel(scale, baseMag)
ScaleBarWidget::setScale(metersPerPixel, viewportScale)
        │  micronsPerPixel = metersPerPixel * 1e6 / viewportScale
        │  pickScaleBarLength(micronsPerPixel, maxBarPixels)
        ▼
ScaleBarWidget::paintEvent  →  line + end caps + label
```

## 1. Pure helper — `pickScaleBarLength`

In `src/core/include/slideio/viewer/core/ScaleBar.h` (new header):

```cpp
namespace slideio::viewer::core
{

struct ScaleBarTick
{
    double lengthMicrons = 0.0;  // 0.0 when input is invalid (caller hides bar)
    int    pixelWidth   = 0;     // matching screen pixels, rounded
};

/// Pick the largest "nice" length (1/2/5 series) whose screen width does
/// not exceed maxBarPixels. Returns {0, 0} only when micronsPerPixel <= 0.
ScaleBarTick pickScaleBarLength(double micronsPerPixel, int maxBarPixels);

} // namespace slideio::viewer::core
```

Candidate lengths (µm), monotonically increasing:

```
0.1, 0.2, 0.5,
1, 2, 5,
10, 20, 50,
100, 200, 500,
1000, 2000, 5000,
10000, 20000, 50000,
100000
```

Algorithm: pick the **largest** candidate `L` such that `L / micronsPerPixel ≤ maxBarPixels`. If even the smallest candidate (0.1 µm) overflows `maxBarPixels` (zoomed in so deep that 0.1 µm is wider than the slider), still return the 0.1 µm candidate — the bar simply extends to its computed pixel width, which may exceed `maxBarPixels`. If the largest candidate (100 000 µm) underflows (zoomed out so far the bar would be < 1 px), still return that candidate — the bar shrinks to its computed width. Callers may clamp visually as needed; the `ScaleBarWidget` does not clamp because in practice both extremes are off the magnification slider's reachable range.

Edge case: `micronsPerPixel ≤ 0` returns `{0.0, 0}` and the caller hides the bar.

Label formatting (done in the widget, not the pure function): one decimal for lengths < 1 µm (`"0.5 µm"`); integer otherwise (`"100 µm"`, `"10000 µm"`). The unit suffix is always `µm` encoded as UTF-8 (`\xC2\xB5m`).

## 2. New widget — `ScaleBarWidget`

In `src/ui/include/slideio/viewer/ui/ScaleBarWidget.h` (new) and `src/ui/src/ScaleBarWidget.cpp` (new):

```cpp
namespace slideio::viewer::ui
{

class ScaleBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScaleBarWidget(QWidget* parent = nullptr);
    ~ScaleBarWidget() override;

    /// metersPerPixelNative: SlideInfo::resolutionX (level 0 pixel size).
    /// viewportScale:        viewport.scale() (1.0 = native).
    /// Either ≤ 0 hides the widget.
    void setScale(double metersPerPixelNative, double viewportScale);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_metersPerPixelNative = 0.0;
    double m_viewportScale        = 0.0;
};

} // namespace slideio::viewer::ui
```

**Geometry**: `setFixedHeight(28)`. The widget's drawable width equals its `width()`; the bar itself is left-aligned within an inset region matching the slider's groove (left/right inset = 6 px, the QSlider handle half-width). This ensures the bar's pixel positions visually align with the slider groove despite the QSlider widget consuming additional margin for its handle.

**Rendering** (inside `paintEvent`):

```
   x0 = handleInset                     ← x0 = 6
   x1 = x0 + tick.pixelWidth            ← clamped to widget width − 6
   y_bar = 4

   Horizontal line:   QPen 1 px, color #CCCCCC, from (x0, y_bar) to (x1, y_bar)
   End caps:          1 px vertical ticks, length 6 (i.e. y_bar−3 .. y_bar+3),
                      at x = x0 and x = x1
   Label:             QFont 10 px, color #CCCCCC, drawn with QPainter::drawText
                      centered horizontally at (x0+x1)/2, top edge at y_bar + 6
   Background:        transparent (no fillRect)
```

The widget reuses Qt's default font; only the point size is overridden (matching `ZoomIndicatorWidget::m_percentageLabel` styling at 10 px).

**Visibility**: when `setScale` receives either input ≤ 0, the widget calls `setVisible(false)`. Otherwise `setVisible(true)` and `update()`.

## 3. Changes to `ZoomIndicatorWidget`

`src/ui/include/slideio/viewer/ui/ZoomIndicatorWidget.h`:

- Add `void setResolution(double metersPerPixel);` (public).
- Add `ScaleBarWidget* m_scaleBar;` (member).
- Add `double m_metersPerPixel = 0.0;` (member).
- Forward-declare `class ScaleBarWidget;`.

`src/ui/src/ZoomIndicatorWidget.cpp`:

- Construct `m_scaleBar` and insert it into the existing `QVBoxLayout` between `labelsLayout` and `m_slider`.
- Increase `setFixedHeight(60)` to `setFixedHeight(88)` (60 + 28 for the new row). The widget remains fixed-width 200.
- Initially hide the bar widget (`m_scaleBar->setVisible(false)`).
- Implement `setResolution(double mpp)` to store `m_metersPerPixel = mpp` and call `m_scaleBar->setScale(m_metersPerPixel, m_currentScale)`.
- At the end of `setZoomLevel(scale, baseMag)`, call `m_scaleBar->setScale(m_metersPerPixel, scale)` so the bar relabels on every viewport change.

The `ZoomIndicatorWidget` does **not** call `setFixedHeight` conditionally on visibility. When the bar is hidden, the widget still reserves 88 px; the bottom row remains empty. (Variable-height overlays would interact awkwardly with `ViewportWidget`'s overlay positioning code, which assumes a stable `sizeHint`. The 28 extra px is negligible against the 60 px baseline.)

## 4. Changes to `MainWindow`

`src/ui/src/MainWindow.cpp`:

- In the `slideOpened` lambda (around line 244): after `setZoomLevel` is called, also call `zoomIndicatorWidget->setResolution(info.resolutionX);`.
- In the `slideClosed` lambda (around line 311): add `zoomIndicatorWidget->setResolution(0.0);` to hide the bar.
- Remove the existing dead line `statusBarManager->updateScaleBar(0.0);` (the call is on line 316; the producing function is also being removed — see § 5).

No other handlers need updating: the existing `viewportChanged` signal already drives `zoomIndicatorWidget->setZoomLevel(...)`, which now also refreshes the bar.

## 5. Dead-code removal in `StatusBarManager`

The current `StatusBarManager::updateScaleBar(double)` is dead: only ever called with `0.0` on slide close, no producer wires real resolution, and the function only formats text (no graphical bar). The new `ScaleBarWidget` supersedes it.

- `src/ui/include/slideio/viewer/ui/StatusBarManager.h`: remove `void updateScaleBar(double resolutionMPP);` declaration and `QLabel* m_scaleBarLabel;` member.
- `src/ui/src/StatusBarManager.cpp`: remove the `updateScaleBar` definition, the `m_scaleBarLabel` initializer, and the corresponding `setup()` block that creates and adds the label.
- `src/ui/src/MainWindow.cpp`: remove the `statusBarManager->updateScaleBar(0.0)` call (already covered in § 4).

## 6. CMake wiring

`src/core/CMakeLists.txt`: add `src/ScaleBar.cpp` to the `slideio-viewer-core` target's sources (header `include/slideio/viewer/core/ScaleBar.h`). The implementation is one short free function — no new dependencies.

`src/ui/CMakeLists.txt`: add `src/ScaleBarWidget.cpp` to the `slideio-viewer-ui` target's sources (header alongside the existing UI headers).

`tests/CMakeLists.txt`: append `core/ScaleBarTest.cpp` to the `slideio-viewer-core-tests` executable's source list (the file naming matches `HistogramTest.cpp`, `ViewportTest.cpp`, etc.).

## 7. Testing

**Unit tests** (`tests/core/ScaleBarTest.cpp`, Catch2-style to match existing core tests — `TEST_CASE(...)`, `REQUIRE(...)`):

| Scenario | Input `(mpp, max)` | Expected `{µm, px}` |
|----------|-------------------:|--------------------:|
| 40× brightfield, native zoom | `(0.25, 172)` | `{20, 80}` |
| 20× brightfield, native zoom | `(0.5, 172)` | `{50, 100}` |
| 40× zoomed in 10× | `(0.025, 172)` | `{2, 80}` |
| 40× zoomed in 100× | `(0.0025, 172)` | `{0.2, 80}` |
| Zoomed out (mpp = 10) | `(10, 172)` | `{1000, 100}` |
| Invalid input | `(0, 172)` | `{0, 0}` |
| Invalid input (negative) | `(-1, 172)` | `{0, 0}` |
| Below floor (deep zoom-in) | `(0.0001, 172)` | `{0.1, 1000}` *(returns smallest, pixel width overflows)* |
| Above ceiling (very far out) | `(1000, 172)` | `{100000, 100}` |

Each row asserts both the chosen length and the rounded pixel width.

**Manual testing** (the project has no UI test framework, per CLAUDE.md):

1. Open a brightfield slide with known resolution (e.g. an Aperio SVS at 40×) — bar appears, label is a sensible round number.
2. Pan the slide — bar stays unchanged (only zoom changes the µm/screen-px).
3. Zoom in and out across the full slider range — bar discrete-snaps through candidates; the bar never extends past the slider groove on either side for typical reachable zooms.
4. Open a slide with no resolution data (`resolutionX == 0`) — bar is hidden; zoom controls still work.
5. Close the slide — bar disappears. Reopen another resolved slide — bar reappears with the correct length.
6. Switch between scenes / Z / T — no spurious bar relabeling (only viewport scale changes do that).

## 8. Risks & considerations

- **Fixed widget height bump (60 → 88 px).** This makes the overlay slightly taller. The overlay anchors at a fixed offset from the viewport corner (set by `ViewportWidget`'s overlay positioning); the bump may visually overlap other overlays (e.g. minimap) on small viewports. Reviewed against current overlay positioning — the zoom indicator and minimap are placed at different corners, so no collision.
- **Slider groove inset assumption.** The 6 px handle half-width is inferred from the slider's stylesheet (`width: 12px; border-radius: 6px;`). If the stylesheet changes, the bar's `x0` will need to move accordingly. The constant is named `kHandleInset` in the widget for visibility.
- **No live `resolutionX` updates.** Per project convention, `SlideInfo` is snapshot once on `slideOpened`; the bar inherits this lifecycle. If a future feature lets users override resolution (e.g. recalibration), `ZoomIndicatorWidget::setResolution` is the entry point.
- **µm-only labels.** Per user requirement, labels stay in µm even at 100 000 µm (= 100 mm). A future units toggle (`Preferences → Display → Scale bar units`) can be added without disturbing the algorithm.
