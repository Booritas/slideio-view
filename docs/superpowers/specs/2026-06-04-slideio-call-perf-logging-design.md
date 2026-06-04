# Performance Logging for Non-Trivial slideio Calls — Design

**Date:** 2026-06-04
**Status:** Approved (design)

## Goal

Extend the existing opt-in perf logging to time the non-trivial `slideio` library
calls (metadata reads, driver/scene/aux enumeration and construction), so their cost
is visible when diagnosing slow slide handling.

## Non-Goals

- No new UI. Reuses the `perf` logger and its on/off control (`SLIDEIO_PERF_LOG` env
  var or Settings → Logs → Performance logging).
- Does not time trivial stored-scalar getters (`getRect`, `getNumChannels`,
  `getMagnification`, etc.) — they return cached fields and only add log noise.
- Does not re-time `::slideio::openSlide` (already timed via `openSlideTimed`) or the
  tile reads `readResampled` / `readResampledBlockChannels` (already timed per tile in
  `TileLoadScheduler`).

## Background

All `slideio` library access is isolated in `src/infra/src/SlideIOAdapter.cpp` (the
architecture's slideio boundary). The `perf` logger (spdlog logger `"perf"`, OFF by
default) and its guards `slideio::viewer::infra::perfLog()` / `perfLogEnabled()` already
exist; `openSlideTimed` (added previously) times each `::slideio::openSlide` call. This
feature adds a generic timing wrapper for the remaining non-trivial calls.

## Component 1 — Generic timing helper

**File:** `src/infra/src/SlideIOAdapter.cpp`

Add a file-local template helper in `namespace slideio::viewer::infra`, next to the
existing `openSlideTimed`:

```cpp
template <typename F>
static auto timeSlideio(const char* method, F&& fn) -> decltype(fn())
```

Behavior:
- Records `start = steady_clock::now()`, invokes `fn()`, and returns its result.
- When `perfLogEnabled()`, logs one trace line via `perfLog()`:
  `slideio::<method>: <ms>ms`.
- Supports both value-returning and `void` calls via `if constexpr
  (std::is_void_v<decltype(fn())>)`, so it is safe for any call site (the chosen set is
  all value-returning, but the wrapper stays general).

Requires `<chrono>` and `<type_traits>` (and the already-present `PerfLog.h`).
`<chrono>` was added with `openSlideTimed`; `<type_traits>` is already included in this
file.

## Component 2 — Wrapped call sites (10)

Each call below is wrapped as `timeSlideio("<label>", [&]{ return <call>; })`. Line
numbers are approximate (they will shift as edits are applied).

| Site | Enclosing function | Call | Label |
|------|--------------------|------|-------|
| ~475 | scene ctor | `m_slide->getScene(sceneIndex)` | `Slide::getScene` |
| ~641 | scene ctor | `m_slide->getMetadata()` (inside `convertMetadata(...)`) | `Slide::getMetadata` |
| ~642 | scene ctor | `m_scene->getMetadata()` (inside `convertMetadata(...)`) | `Scene::getMetadata` |
| ~666 | aux-image ctor | `m_slide->getAuxImage(auxImageName)` | `Slide::getAuxImage` |
| ~818 | aux-image ctor | `m_slide->getMetadata()` (inside `convertMetadata(...)`) | `Slide::getMetadata` |
| ~819 | aux-image ctor | `m_scene->getMetadata()` (inside `convertMetadata(...)`) | `Scene::getMetadata` |
| ~847 | `enumerateScenes` | `slide->getScene(i)` | `Slide::getScene` |
| ~868 | `enumerateScenes` | `slide->getAuxImageNames()` | `Slide::getAuxImageNames` |
| ~871 | `enumerateScenes` | `slide->getAuxImage(auxName)` | `Slide::getAuxImage` |
| ~1125 | `SlideIOAdapter::availableDriverIds` | `::slideio::getDriverIDs()` | `getDriverIDs` |

The two `getMetadata` calls are arguments to `convertMetadata(...)`; the wrap goes
around the inner slideio call, e.g.
`convertMetadata(timeSlideio("Slide::getMetadata", [&]{ return m_slide->getMetadata(); }))`.

**Explicitly not wrapped** (trivial scalar getters): `getRect`, `getNumChannels`,
`getChannelDataType`, `getMagnification`, `getResolution`, `getCompression`,
`getNumZSlices`, `getNumTFrames`, `getChannelName`, `getNumZoomLevels`, `getLevelInfo`,
`getDriverId`, `getNumScenes`, `getName`.

## Log Output

```
slideio::Slide::getScene: 0.3ms
slideio::Slide::getMetadata: 4.1ms
slideio::Scene::getMetadata: 2.8ms
slideio::getDriverIDs: 0.1ms
...
```

These interleave with the existing `slideio::openSlide '...'` and the end-to-end
`openSlide '...' end-to-end` lines, giving a per-call breakdown of an open.

## Error Handling

- `timeSlideio` does not catch exceptions: if `fn()` throws, the exception propagates
  exactly as the un-wrapped call would (callers keep their existing try/catch — e.g. the
  `getMetadata` calls are already inside a try block, and `enumerateScenes` wraps its
  body in try/catch). No timing line is emitted on the throwing path, which is correct
  (the call did not complete).

## Testing

- **No unit test.** The wrapper times real slideio I/O; there is no pure logic to test
  (consistent with `openSlideTimed` and the tile-read timing).
- **Manual verification** (perf logging enabled via `SLIDEIO_PERF_LOG=1` or Settings →
  Logs → Performance logging):
  - Open a local slide. The log shows `slideio::Slide::getScene`, `Slide::getMetadata`,
    `Scene::getMetadata`, and (at app start / open) `slideio::getDriverIDs` lines, plus
    the `enumerateScenes` `getScene`/`getAuxImageNames`/`getAuxImage` lines.
  - With perf logging OFF (default), none of these lines appear.
  - Numbers are plausible (metadata parse > trivial getter; remote slides larger).

## Notes for the implementer

- **DRY vs `openSlideTimed`:** keep `openSlideTimed` as-is — it logs richer context
  (file path, driver, open context). `timeSlideio` is the generic wrapper for the other
  calls. The minor duplication (two small timing helpers) is acceptable and clearer than
  forcing one signature to do both.
- **Lambda capture:** use `[&]` — the call executes synchronously inside `timeSlideio`,
  so capturing by reference is safe (no escape).
- **Layering / cost:** unchanged from prior perf work — `PerfLog.h` is in `infra`; the
  only unconditional cost per wrapped call is two `steady_clock` reads; formatting is
  behind `perfLogEnabled()`.
- **`{}` format:** `{:.1f}` for milliseconds, matching existing perf lines.
```
