# Colour management of the tile pipeline — design

**Date:** 2026-10-06
**Status:** Implemented
**Branch:** `color-management`, based on `main` @ `54ac47a`

## Goal

Let the viewer convert slide pixels into sRGB through the slide's ICC profile, so
that what a pathologist sees on screen is colorimetrically defined rather than
raw scanner RGB.

Conversion is driven by SlideIO's `ColorManagement` transformation, applied by
wrapping the open scene with `transformScene`. It is controlled by a single
checkable menu item, off until the user asks for it, and available only on slides
where ICC conversion actually means something.

Where a slide embeds no profile, the user can nominate one profile that stands in
for every such slide — the realistic case, since a scanner that embeds nothing
embeds nothing for its whole output.

This builds on `54ac47a`, which already reports the parsed profile in the slide
properties panel. That work supplies the `present` flag this feature gates on.

## Non-goals

- **No display/monitor profile.** Converting the composited image for the
  viewer's own monitor is a separate feature, applied after channel mixing in the
  fragment shader, and uses no SlideIO. Out of scope.
- **No multichannel support, in any form.** `ColorManagement::bindToSource`
  rejects anything but three channels of `DT_Byte`/`DT_UInt16`
  (`colormanagement.cpp:35-45`), and SlideIO is right to: a fluorescence channel
  is an intensity measurement, not a colour, and no profile makes an ICC
  transform of one mean anything. A supplied profile does not change this — the
  channel check runs before the profile is examined.
- **No Lab/XYZ/LinearRGB targets.** A viewer displays to a screen; the target is
  sRGB. The other `ColorTarget` values produce `DT_Float32` blocks the render
  path cannot consume.
- **No per-slide profile override.** One global default only. Per-slide
  assignment can be added later if the workflow needs it. *Retired 2026-10-07:
  see `2026-10-07-per-slide-color-profile-design.md`. The reasoning that kept
  this out of scope at the time -- one global setting covers a scanner that
  embeds nothing for its whole output -- is still worth reading; the later
  feature only adds the one case it left unserved.*
- **No exposed rendering intent or black-point compensation.** SlideIO's defaults
  (relative colorimetric, BPC on) are used and not surfaced.

## Background: what SlideIO provides

Verified against the submodule at the pinned revision.

| Fact | Where |
|---|---|
| `transformScene(scene, ColorManagementWrap&)` returns a wrapped `Scene` | `transformer.cpp:60-66` |
| Binding happens in the `TransformerScene` constructor, so an unsupported scene throws **at wrap time**, not on first tile read | `transformerscene.cpp:42` |
| The wrapped scene copies the origin's pyramid verbatim — geometry, scale and magnification all preserved | `transformerscene.cpp:65-68` |
| 4D reads need no special handling: `CVScene::readResampled4DBlockChannels` assembles planes through the virtual `readResampledBlockChannelsEx`, which `TransformerScene` overrides | `cvscene.cpp:177-181` |
| A supplied override wins over an embedded profile unconditionally | `colormanagement.cpp:48` |
| SlideIO stamps `ColorProfileSource::Supplied` on a valid override itself — the caller must not, because `ColorProfile`'s constructor stamps `Embedded` for any non-empty byte vector | `colormanagement.cpp:60-65`, `colorprofile.cpp:12` |
| A corrupt override degrades to absence and is then governed by `MissingProfilePolicy` | `colormanagement.cpp:51-55` |
| A non-RGB profile throws with its own clear message | `colormanagement.cpp:56-58` |
| The wrapped scene delegates `supportsConcurrentReads()` to the origin and returns the **origin's** serialisation mutex, so a wrapped read and a direct read exclude each other correctly | `transformerscene.hpp:71-88` |

Drivers that populate a profile: DCM, GDAL, NDPI, OME-TIFF, PKE, SCN, SVS, VSI.
Not CZI, AFI or ZVI — those always report `None`.

## Architecture

`SlideIOAdapter` holds both scenes and selects between them per read. Nothing
above infra learns that two scenes exist.

```
                    SlideIOAdapter
                    ├── m_scene         (origin, as today)
                    ├── m_managedScene  (transformScene wrapper, or null)
                    └── m_colorMode     (atomic: Raw | Managed)

  readTile(key) ──► scene = (mode == Managed && m_managedScene)
                            ? m_managedScene : m_scene
                     └──► scene->readResampledBlockChannels(...)
```

Both scenes share one underlying `CVScene`: `TransformerScene` holds the origin
by `shared_ptr` and borrows its serialisation mutex. There is no second file
handle and no second reader, so the project's one-shared-scene policy is intact.

### Why colour mode belongs in `TileKey`

The obvious implementation — flip a flag, clear the cache — has a race.
`TileLoadScheduler::cancelAll` drains only the pending queue
(`TileLoadScheduler.cpp:80-89`); a worker already inside `readTile` finishes
afterwards and inserts a tile read in the *old* mode into the freshly cleared
cache. The result is a stale tile of the wrong colour, appearing intermittently
and only under load.

Putting the mode in `TileKey` removes the race by construction, because a raw
tile and a managed tile of the same region become different cache entries *and*
because the key is what the adapter selects the scene by:
`SlideIOAdapter::readTile` reads through `sceneForMode(key.colorMode())`, never
through the adapter's live mode. A read that was enqueued before a toggle and
finishes after it therefore still produces the rendition its key names, and the
cache files it under that same key — pixels and key correspond by construction,
not by timing. (Selecting by the live mode instead is precisely the bug this
keying is meant to exclude: it would let a read that straddled a toggle be
cached as though it were the other rendition, and the tile would stay wrong
until eviction.) Two further benefits fall out: no cache clear is needed at all, and
toggling back and forth is instant because both sets remain cached — which
matters, since A/B comparison is the main reason to have a toggle.

The cost is up to 2× cache occupancy for a slide the user has actually toggled.
The LRU budget absorbs this by evicting the colder set.

`TileKey` is constructed in only four files outside its own header and
implementation — `TilePyramid.cpp`, `ViewportController.cpp`,
`ViewportWidget.cpp` and `Prefetcher.cpp` — so the churn is contained. Which of
those sites have to stamp the mode, and which are correct leaving the default
`Raw` on, is set out under **Key construction** below.

### Why capability is probed, not predicted

Because binding happens inside `transformScene`, the adapter can attempt the wrap
once at open and record what happened. The menu item's enabled state is then a
fact rather than a re-implementation of SlideIO's validation rules, which would
drift.

The one rule the viewer must enforce *itself* is the fluorescence gate below —
SlideIO does not catch it.

## 1. `core` — colour mode on `TileKey`

In `src/core/include/slideio/viewer/core/TileKey.h`:

```cpp
enum class ColorMode { Raw, Managed };

class TileKey
{
public:
    TileKey();
    TileKey(int level, int column, int row, int zIndex = 0, int tFrame = 0,
            ColorMode colorMode = ColorMode::Raw);

    ColorMode colorMode() const;
    // ... existing accessors unchanged
};
```

`operator==` and `std::hash<TileKey>` are extended with the new field, following
the existing hash-combine pattern. `toString()` appends the mode so log lines
remain unambiguous.

The parameter is defaulted so existing call sites compile, but all five are
updated to pass the viewport's current mode explicitly. The default exists for
tests and for the degenerate `TileKey()` constructor, not as a licence to omit it.

## 2. `core` — availability and its reasons

In a new `src/core/include/slideio/viewer/core/ColorManagement.h`:

```cpp
enum class ColorManagementAvailability
{
    Available,
    NotColorimetric,  ///< not 3-channel Byte/UInt16, or fluorescence
    NoProfile,        ///< colorimetric, but no embedded profile and no default
    BindFailed,       ///< SlideIO refused the wrap; message carries the reason
};

/// Tooltip text for a disabled "Color management" menu item.
std::string colorManagementUnavailableReason(ColorManagementAvailability a,
                                             const std::string& detail);
```

**Refinement from the agreed design:** the separate `ProfileNotRgb` value is
dropped. SlideIO's own message already reads "the source profile describes CMYK
data, not RGB", and `BindFailed` carrying `detail` reports that better than a
value we would have to infer. A non-RGB *default* profile is caught earlier
anyway, at pick time, by section 3.

## 3. `core` — ICC header inspection

In the same header:

```cpp
struct IccHeaderSummary
{
    bool plausible = false;     ///< parses as an ICC profile header
    std::string dataSpace;      ///< 4-char signature, e.g. "RGB ", "CMYK"
    size_t declaredSize = 0;
};

IccHeaderSummary inspectIccHeader(const std::vector<uint8_t>& bytes);
```

Checks, per ICC.1:2010 §7.2: length ≥ 128; `acsp` at offset 36; declared profile
size at offset 0 consistent with the buffer. Returns the data colour space from
offset 16.

This exists so that choosing a default profile fails *at the file dialog*, with a
message naming the problem, rather than silently degrading to `Assumed` several
slides later. It is a sanity check, not a parser — SlideIO and lcms2 remain the
authority.

## 4. `core` — `SlideInfo` additions

```cpp
ColorManagementAvailability colorManagement = ColorManagementAvailability::NotColorimetric;
std::string colorManagementDetail;   // SlideIO's message when BindFailed
bool fluorescenceHint = false;       // surfaced from the adapter
```

`fluorescenceHint` is currently a local in `SlideIOAdapter.cpp:508`, consumed by
the `isBrightfield` heuristic and then discarded. It must be exposed because
`isBrightfield` is the wrong gate in both directions: it is false for 16-bit RGB
brightfield, which `ColorManagement` *does* support, and the hint is what
separates a 3×8-bit fluorescence image from a brightfield one.

## 5. `infra` — `SlideIOAdapter`

New members:

```cpp
std::shared_ptr<::slideio::Scene> m_managedScene;   // null when unavailable
std::atomic<core::ColorMode>      m_colorMode{core::ColorMode::Raw};
```

New method, added to `core::ISlideSource` with a no-op default so other backends
are unaffected:

```cpp
virtual void setColorMode(ColorMode) {}
virtual ColorMode colorMode() const { return ColorMode::Raw; }
```

### Gate, evaluated at open

```
colorimetric = numChannels == 3
            && channelDataType ∈ { Byte, UInt16 }
            && !fluorescenceHint

haveProfile  = colorProfileInfo.present || defaultProfile is configured
```

- `!colorimetric` → `NotColorimetric`, no wrap attempted.
- `colorimetric && !haveProfile` → `NoProfile`, no wrap attempted.
- otherwise → attempt the wrap; success → `Available`, throw → `BindFailed` with
  the exception text.

The fluorescence term is the correctness-critical one. A 3-channel 8-bit
fluorescence image passes SlideIO's own check and would be ICC-transformed as
though its intensity channels were R/G/B.

### Wrapping

```cpp
::slideio::ColorManagementWrap cm;
cm.setTarget(::slideio::ColorTarget::sRGB);
cm.setMissingProfilePolicy(::slideio::MissingProfilePolicy::Fail);
if (!defaultProfileBytes.empty() && !sceneProfileInfo.present) {
    cm.setSourceProfileOverride(::slideio::ColorProfile(defaultProfileBytes));
}
m_managedScene = ::slideio::transformScene(m_scene, cm);
```

`MissingProfilePolicy::Fail` is deliberate. The gate guarantees a profile is
present, so this path should be unreachable; `Fail` turns a gate bug into a clean
"unavailable" at open instead of an identity transform that silently claims to be
colour-managed. `AssumeSRGB` would hide exactly the bug we most want to see.

The override is applied only when the scene embeds nothing. SlideIO would let it
displace an embedded profile (`colormanagement.cpp:48`), which is not what a
"default for slides that have none" setting should do.

The adapter receives `defaultProfileBytes` through its constructor. Infra does no
file I/O and no settings lookup.

### Reads

```cpp
const auto& scene = (m_colorMode.load(std::memory_order_relaxed) == core::ColorMode::Managed
                     && m_managedScene) ? m_managedScene : m_scene;
```

Applied in both `readTile` and `readBlock`. Relaxed ordering suffices: correctness
comes from the mode being part of the cache key, not from the load's ordering.

Eager construction costs one lcms2 transform build per qualifying slide, on a
path that already reads metadata, levels and histograms. Section 9 measures it.

## 6. `infra` — build

`cmake/FindSlideIO.cmake` gains a `SlideIO::transformer` imported target,
following the existing `SlideIO::core` block exactly: per-config release/debug
library lookup (`slideio-transformer` / `slideio-transformer_d`), the same cache
unsetting, the same Windows import-lib-in-`lib`/DLL-in-`bin` split.

`src/infra/CMakeLists.txt` adds it to the existing `PRIVATE` link line. Runtime
deployment needs no change: `CMakeLists.txt:203` already installs all of
`${SLIDEIO_ROOT}/release/bin`, which includes `slideio-transformer.dll`.

## 7. `ui`

**View menu**

| Item | Behaviour |
|---|---|
| `Color management` | Checkable, `Ctrl+Shift+I` (not `Ctrl+Shift+C`: that combination is already the Channels panel toggle in `MainWindow.cpp`'s panel-toggle setup — do not "restore" `Ctrl+Shift+C` here). Disabled when availability ≠ `Available`, with `colorManagementUnavailableReason()` as its tooltip. |
| `Set default ICC profile…` | File dialog (`*.icc *.icm`). Rejects a file failing `inspectIccHeader`, or whose `dataSpace` is not `"RGB "`, with a message naming the reason. |
| `Clear default ICC profile` | Enabled only when one is set. |

**Settings** (`QSettings`)

- `view/colorManagement` — bool, initially `false`, persisted once the user sets
  it. "Off by default" is the initial default, not a reset-every-launch.
- `color/defaultSourceProfile` — the *path*, not the bytes. The bytes are read
  from that path by `MainWindow::applyDefaultColorProfile()` at construction and
  whenever the setting changes, and cached in the viewport for the open paths to
  copy; they are *not* re-read per slide open, so replacing the file on disk has
  no effect until the app restarts or the user re-picks it. An unreadable path
  logs a warning and is treated as unconfigured. The header is validated only
  when the user picks the file, so a profile that later goes stale or corrupt is
  re-read unvalidated at startup and fails at bind time as `BindFailed`.

**Key construction** — `TilePyramid` yields geometry only (level, column, row)
and leaves the default `Raw` mode on the keys it returns; the callers that feed
rendering or the cache stamp the active mode on. That is
`ViewportController::visibleTileKeys()` and, in `ViewportWidget`, *both* coarse
background passes in `paintGL` — the single-channel brightfield one and the one
in the catch-all `else` branch, which is the path every colour-managed slide
actually takes, since `isColorimetricForIcc` requires three channels. The two
passes stamp identically; keep them that way. The remaining `TileKey` sites in
`ViewportWidget` are open-time thumbnail, autodetect and base-layer reads: those
are deliberately raw and their default-`Raw` keys are correct for them.
`Prefetcher` also builds keys without a mode; it is not wired into the viewport
today, so nothing it builds reaches the scheduler or the cache. It will need the
same stamp before it is switched on.

**Toggle handling** — set the adapter's mode, set the viewport's mode, repaint.
No cache clear is needed. (`ViewportController::requestVisibleTiles()` does call
`m_scheduler->cancelAll()` to drop superseded requests, as it does on any view
change; that is unrelated to colour mode, and correctness does not depend on it.)

**Thumbnails and the minimap overview** are raw in practice, and deliberately
so. They come from `readBlock`, which carries no `TileKey` and is never cached
under one, and from the open-time raw tile reads above. Note that `readBlock` is
*not* itself mode-independent: it routes through `activeScene()`, so it returns
whichever rendition the adapter's mode selects at the moment of the call. What
makes these surfaces stable is *when* they run — all four `readBlock` call sites
are at open time or on temp adapters still in `Raw`, so no toggle can reach
them. They are navigation aids, not diagnostic surfaces, and a single stable
appearance is what we want for them. Refreshing the minimap from the live
adapter later would break that and would need `sceneForMode(ColorMode::Raw)`
rather than `activeScene()`.

**Status bar** — an indicator reading `sRGB` when managed, absent when raw.

**Properties panel** — refreshed on toggle so the `Source` row from `54ac47a`
tracks `Embedded` / `Supplied` / `Assumed` as the mode changes.

This needs one addition, because the two scenes report different provenance. The
origin reports what the file carries; the wrapper overrides `getColorProfile()`
to return the profile it actually bound (`transformerscene.hpp:41`), which for a
profile-less slide with a default configured is the supplied one. `SlideInfo` is
captured once at open from the origin, so it cannot answer this. The adapter
therefore gains:

```cpp
core::ColorProfileInfo activeColorProfileInfo() const;  // of the scene currently selected
```

and the panel calls it on toggle. `SlideInfo::colorProfileInfo` keeps its current
meaning — what the file embeds — and is unchanged.

## 8. Error handling

Every failure resolves to "unavailable, with a stated reason". Nothing silently
does nothing.

| Situation | Outcome |
|---|---|
| Fluorescence, non-RGB channel count, or unsupported data type | `NotColorimetric`; wrap never attempted |
| No embedded profile and no default configured | `NoProfile` |
| Wrap throws (non-RGB profile, corrupt profile under `Fail`, anything else) | `BindFailed`; SlideIO's message in the tooltip and the log |
| Chosen default file is not a plausible ICC profile, or is not RGB | Rejected at the file dialog; setting unchanged |
| Configured default path unreadable at slide open | Warning logged; treated as unconfigured, so availability becomes `NoProfile` |
| Per-tile read error in managed mode | Existing `readTile` error handling; unchanged |

## 9. Testing

**Unit (core, TDD — test first, watch it fail):**

- `TileKey`: two keys differing only in colour mode are unequal and hash apart;
  equal keys with equal modes still compare equal; `toString()` names the mode.
- `inspectIccHeader`: valid 128-byte header; truncated buffer; wrong `acsp`
  signature; declared size disagreeing with the buffer; empty input; data space
  extraction for `RGB ` and `CMYK`.
- `colorManagementUnavailableReason`: one case per enum value, including that
  `BindFailed` includes its detail string.

**Integration (infra):** the conversion test does not use `gdal/colors.png` as
originally planned here. `colors.png` embeds a plain "GIMP built-in sRGB"
profile, and `Managed` mode always targets sRGB, so converting it is an
identity transform that can never demonstrate a real conversion. Instead:
open `svs/JP2K-33003-1.svs` (Aperio, a profile that actually disagrees with
sRGB), read one tile in each mode, assert the pixel data differs; `colors.png`
is kept as the fixture for a deliberate identity-transform regression test
instead, asserting raw and managed bytes are equal. Also: open
`img_2448x2448_3x8bit_SRC_RGB_ducks.png` with a default profile configured and
assert it is reported as `Supplied`; assert a fluorescence fixture reports
`NotColorimetric`.

This requires `slideio-viewer-infra-tests` to find the SlideIO DLLs at runtime —
the `ENVIRONMENT_MODIFICATION` treatment `ui-tests` already gets at
`tests/CMakeLists.txt:52-60`. It contradicts the comment at
`tests/CMakeLists.txt:46` stating infra tests pull in no SlideIO symbols; that
comment is updated as part of this change. **Approved in review as a deliberate
departure** — it is the only test that proves the feature converts anything.

**Manual:** an SVS or NDPI with an embedded profile (both modes, confirm the
image visibly changes); a profile-less brightfield slide with and without a
default configured; a fluorescence slide (menu item greyed, correct tooltip); a
CZI (greyed, `NoProfile`).

## 10. Performance

Measured on `svs/JP2K-33003-1.svs` (15374×17497, Aperio profile), Release
build, warm filesystem cache, three runs each via a temporary timing harness
(not part of the shipped code).

- **Slide open.** `SlideIOAdapter` construction, with the eager
  `buildManagedScene` wrap versus with the `transformScene` call suppressed:

  | | Run 1 | Run 2 | Run 3 | Mean |
  |---|---|---|---|---|
  | With eager wrap | 34.00 ms | 34.07 ms | 33.96 ms | **34.01 ms** |
  | Without eager wrap | 1.04 ms | 1.00 ms | 0.93 ms | **0.99 ms** |

  Eager wrapping adds ≈33.0 ms, a ≈34× (≈3340%) increase over the
  wrap-free construction cost — the opposite of the "dominated by existing
  metadata and histogram work" expectation above, and far past the ~5%
  threshold for staying eager. The cost is building the lcms2 transform
  itself (`transformScene` → `ColorManagementWrap::bindToSource`), not
  anything already-planned metadata work hides it behind.

- **Tile read.** `readTile`, 100 tiles at the same keys, each mode:

  | | Total (100 tiles) | Mean per tile |
  |---|---|---|
  | Raw | 239.69 ms | 2.40 ms |
  | Managed | 402.39 ms | 4.02 ms |

  Managed reads cost ≈1.63 ms more per tile than raw (≈+68%). The lcms2
  transform is immutable after binding and `apply()` is const and re-entrant
  (`colormanagement.hpp:45-47`), so this runs on the existing worker threads
  with no added serialisation — the per-tile cost is the conversion itself,
  not contention.

**Recommendation:** go lazy. Build `m_managedScene` on first
`setColorMode(Managed)` instead of eagerly in the constructor, keeping the
gate (`isColorimetricForIcc` + profile presence) as the availability
predictor exactly as today — only the `transformScene` call itself moves.
This is not a one-line change: `SlideIOAdapter::activeScene()` currently
returns a reference to a member `shared_ptr` and is documented as safe only
because neither `m_scene` nor `m_managedScene` is reassigned after
construction. Lazy construction reassigns `m_managedScene` after
construction (on the UI/toggle thread, read from worker threads via
`activeScene()`), so making that access safe under concurrent reads is a
precondition of going lazy, not a detail to fix in passing. Left to a
follow-up review and its own change, per the brief for this task.

### Follow-up: measured against total slide-open time

The ~5% bar above was written against `SlideIOAdapter` construction alone,
whose wrap-free cost is only ≈1 ms — so almost any fixed cost looks extreme
against it (the ≈3340% figure). The quantity a user actually perceives is
total slide-open time, of which adapter construction is one part: the caller
(`openSceneSync` in `src/ui/src/ViewportWidget.cpp`, not reachable from a test
without editing production code, since it and
`readCoarseLevelAndBuildThumbnail` both have internal linkage in that file's
anonymous namespace) also enumerates levels, builds the `TilePyramid` and
`LruTileCache`, reads every tile of the coarsest pyramid level to autodetect
the display range (all 72 tiles here — under `kMaxCoarseScanTiles` = 256, so
unsampled), and reads one full-slide thumbnail `readBlock`. A proxy harness
reproducing exactly those public-API calls (construction, `levels()`, the
72-tile coarsest-level scan, one `readBlock` sized to fit 1000px), run three
times per process across three process invocations (9 runs total, same slide,
Release, warm cache):

| | Mean | Range (9 runs) |
|---|---|---|
| Total open (proxy), eager wrap as shipped | **830.8 ms** | 769.8–888.7 ms |
| Total open (proxy), without eager wrap (derived: −33.0 ms, this task's earlier construction delta — not re-measured here to avoid touching production code twice) | **797.8 ms** | — |

Eager wrapping's share of total open time is **≈4.0%** — under the ~5% bar,
not over it. Measured against the right denominator, eager wrapping is
immaterial to a user already waiting ~800 ms for the rest of the open to
finish, dominated by the coarsest-level tile scan and the thumbnail
`readBlock`, not by metadata/histogram bookkeeping as such.

**Revised recommendation: stay eager.** The earlier "go lazy" call was an
artifact of comparing against the wrong baseline. At ≈4% of total open time,
eager wrapping is not worth trading for `activeScene()` losing its
construction-time-only reference safety — a real concurrency hazard — for a
saving the user is unlikely to perceive. Shipped behaviour (eager, as already
implemented) is correct as-is; no further change recommended.

One more fact bearing on the denominator: `readCoarseLevelAndBuildThumbnail`
runs on **every** successful slide open (both `openSceneSync` and
`openAuxImageSync` call it unconditionally, each wrapped only in a
try/catch that logs and continues on failure — it is never skipped by slide
type). Its two passes can each shrink independently, though: the coarsest-
level scan is strided down to at most `kMaxCoarseScanTiles` (256) tiles
rather than reading every tile when the coarsest level is larger than that;
the thumbnail `readBlock` is skipped entirely (`overviewAffordable = false`)
when the coarsest level exceeds `kMaxOverviewTiles` (4096) tiles — the case of
a slide with no downsampled pyramid level at all. So the ≈800 ms denominator
measured here is specific to this slide's 8×9 coarsest level; a slide that
hits either cap would see a smaller (capped) version of this same work, not a
skip of the whole pass, except for the `readBlock` half specifically.

## 11. Sequencing

1. `core` — `TileKey` colour mode, availability, `inspectIccHeader`, `SlideInfo`
   fields. Unit tests throughout.
2. `infra` — `FindSlideIO.cmake` and the link line, then the dual scene and gate.
3. `infra` — integration tests.
4. `ui` — key stamping, menu, settings, status bar, panel refresh.
5. Measure, then manual verification across the four slide classes.

Steps 1 and 2 are independently buildable and leave the viewer working with
colour management permanently off, which keeps each commit shippable.
