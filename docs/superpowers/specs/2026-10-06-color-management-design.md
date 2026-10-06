# Colour management of the tile pipeline — design

**Date:** 2026-10-06
**Status:** Draft (awaiting review)
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
  assignment can be added later if the workflow needs it.
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
tile and a managed tile of the same region become different cache entries. A
late-finishing raw read inserts under a raw key that managed-mode rendering never
looks up. Two further benefits fall out: no cache clear is needed at all, and
toggling back and forth is instant because both sets remain cached — which
matters, since A/B comparison is the main reason to have a toggle.

The cost is up to 2× cache occupancy for a slide the user has actually toggled.
The LRU budget absorbs this by evicting the colder set.

`TileKey` has five construction sites outside its own header and implementation
(`TilePyramid.cpp`, `ViewportController.cpp`, `ViewportWidget.cpp`), so the churn
is contained.

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
| `Color management` | Checkable, `Ctrl+Shift+C`. Disabled when availability ≠ `Available`, with `colorManagementUnavailableReason()` as its tooltip. |
| `Set default ICC profile…` | File dialog (`*.icc *.icm`). Rejects a file failing `inspectIccHeader`, or whose `dataSpace` is not `"RGB "`, with a message naming the reason. |
| `Clear default ICC profile` | Enabled only when one is set. |

**Settings** (`QSettings`)

- `view/colorManagement` — bool, initially `false`, persisted once the user sets
  it. "Off by default" is the initial default, not a reset-every-launch.
- `color/defaultSourceProfile` — the *path*, not the bytes, so replacing the file
  on disk takes effect on next open. Bytes are read at slide open; an unreadable
  path logs a warning and is treated as unconfigured.

**Key construction** — `ViewportController` and `ViewportWidget` stamp the active
mode on every `TileKey` they build.

**Toggle handling** — set the adapter's mode, set the viewport's mode, repaint.
No cache clear, no scheduler cancellation.

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

**Integration (infra):** open `gdal/colors.png` (672-byte embedded sRGB profile),
read one tile in each mode, assert the pixel data differs; open
`img_2448x2448_3x8bit_SRC_RGB_ducks.png` with a default profile configured and
assert the same; assert a fluorescence fixture reports `NotColorimetric`.

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

Two measurements, both recorded in the implementation:

- **Slide open:** time to construct `SlideIOAdapter` with and without eager
  wrapping, on a large SVS. Expected to be dominated by existing metadata and
  histogram work. If eager wrapping proves material, fall back to building
  `m_managedScene` lazily on first enable, keeping the gate as the predictor.
- **Tile read:** per-tile `readTile` time in both modes on the same slide. The
  lcms2 transform is immutable after binding and `apply()` is const and
  re-entrant (`colormanagement.hpp:45-47`), so it runs on the existing worker
  threads with no added serialisation.

## 11. Sequencing

1. `core` — `TileKey` colour mode, availability, `inspectIccHeader`, `SlideInfo`
   fields. Unit tests throughout.
2. `infra` — `FindSlideIO.cmake` and the link line, then the dual scene and gate.
3. `infra` — integration tests.
4. `ui` — key stamping, menu, settings, status bar, panel refresh.
5. Measure, then manual verification across the four slide classes.

Steps 1 and 2 are independently buildable and leave the viewer working with
colour management permanently off, which keeps each commit shippable.
