# Per-slide ICC profile override — design

**Date:** 2026-10-07
**Status:** Approved, not implemented
**Base:** `main` @ `dbbc1dd`

## Goal

Let the user nominate an ICC profile for one particular slide, remembered across
sessions, taking precedence over both the global default profile and any profile
the slide embeds.

The colour management feature of `3c6049d` supplies one global default profile,
and applies it only to slides that embed nothing. That is the right rule for a
default: a scanner that embeds no profile embeds none for its whole output, so
one setting covers the lot. It leaves one case unserved — a slide whose embedded
profile is wrong, or which came from a miscalibrated scanner, where the user
holds a better characterisation for that slide alone.

This feature is that case. It also introduces `computeSlideId`, the
content-derived slide identity the architecture already plans for annotation
persistence (`03-software-architecture-and-design.md` §9.1); the override store
is its first consumer, not its only intended one.

## Non-goals

- **No display/monitor profile.** Unchanged from the colour management design:
  converting the composited image for the viewer's own monitor remains a
  separate feature.
- **No per-scene override.** The override is keyed by slide, so every scene in a
  multi-scene file shares it. A profile characterises the scanner that produced
  the file, not one scene within it.
- **No exposed rendering intent or black-point compensation.** SlideIO's
  defaults are still used and not surfaced.
- **No automatic migration of existing settings.** The global default
  (`color/defaultSourceProfile`) keeps its current meaning and behaviour
  untouched.
- **The slide ID is not tamper-evident.** It is a lookup key. See
  "Why FNV-1a, not SHA-256" below.

## Precedence

Evaluated at slide open, highest first:

1. A per-slide override stored for this slide's ID, whose file validates now.
2. The profile the slide embeds.
3. The global default profile, whose file validates now.
4. Nothing — colour management is unavailable for want of a profile.

Rule 1 beating rule 2 is the whole point of the feature, and is the only
precedence change. Rules 2-4 are today's behaviour.

## 1. `core` — slide identity

New `SlideId.h` / `SlideId.cpp` in `slideio-viewer-core`. Pure C++17, no Qt, as
the layer requires.

```cpp
/// A 16-character lowercase hex identity derived from a slide's content
/// properties: stable across moves and renames, independent of file path.
///
/// NOT cryptographic and NOT tamper-evident. It identifies a slide for the
/// user's own stored settings; nothing authenticates against it. A consumer
/// must never treat a matching ID as proof of provenance.
std::string computeSlideId(const std::vector<SceneInfo>& scenes, uint64_t fileSizeBytes);

/// Convenience overload. Delegates to the one above, so a slide identified
/// before it is opened and the same slide identified afterwards agree by
/// construction rather than by two call sites staying in step.
std::string computeSlideId(const SlideInfo& info, uint64_t fileSizeBytes);
```

The hash input concatenates, in this fixed order:

```
fileSizeBytes, scenes.size(),
then for each entry of scenes, in index order:
    index, width, height, numChannels
```

Fields are separated by `\x1F` (ASCII unit separator), which cannot occur in any
of them. Without a separator, width 1 with height 00 and width 10 with height 0
would hash alike.

**`driverId` is deliberately not an input**, for two independent reasons. It is
unresolved before the slide is opened — `""` means auto-detect, and the resolved
name appears only on the `SlideInfo` that opening produces — so including it
would make the identity unavailable at the moment it is needed (see
"Resolution" below). And a file opened by auto-detection and the same file
opened with its driver named explicitly are the same slide; keying them to two
IDs would silently lose an override when the user picks a driver by hand.

**Every input is file-level, not scene-level.** This is what makes the ID the
same from whichever scene of a multi-scene file happens to be open, as the
per-scene non-goal above requires. `SlideInfo::scenes` holds the geometry of
every scene in the file, so the open scene's own `width`, `height`,
`numChannels`, `channelDataType`, `numZoomLevels`, `numZSlices`, `numTFrames`,
`resolutionX` and `resolutionY` are all deliberately excluded — each of them
varies by scene and would key a multi-scene file to several different IDs.

The cost is that pixel resolution, a good discriminator, cannot contribute.
`fileSizeBytes` carries that weight instead: two distinct slides with
byte-identical sizes and an identical scene-geometry table are not a case that
arises outside deliberate construction.

No input is floating-point, so there is no cross-platform formatting question to
settle.

### Resolution: where the ID comes from, and when

The identity is needed *before* the adapter is constructed, because the adapter
takes the profile bytes in its constructor. The inputs are therefore restricted
to what `SlideIOAdapter::enumerateScenes(filePath, driverId)` returns, which is
exactly the `SceneInfo` vector the primitive overload takes, plus the file size
from the filesystem. No slide needs to be opened for the viewer's own purposes
to compute an ID.

Today `ViewportWidget::openSlide` calls `enumerateScenes` *after*
`openSceneSync`, purely because the scene panel is the only consumer. Those two
calls swap order, so enumeration — which already happens on every open — feeds
the override lookup. This adds no file open: it moves one that is already there.

`ViewportWidget::openScene` and `openAuxImage` need no reordering; they run on a
file whose `SlideInfo::scenes` is already populated, so the ID is computable on
the UI thread before the worker starts.

### Divergence from `03-software-architecture-and-design.md` §9.1

That section defines the ID as a hash of `file_size + dimensions + scan_date +
scanner_id`. It cannot be implemented as written: `SlideInfo` carries no
`scanDate` or `scannerModel`, and those values exist only inside the
driver-specific `slideMetadata` / `sceneMetadata` trees, which SVS, CZI and NDPI
populate inconsistently. The field list above replaces them with pyramid
geometry, which every driver reports.

Implementation updates §9.1 to this definition, so that Stage 3's annotation
store inherits a scheme that compiles.

### Why FNV-1a, not SHA-256

Three reasons, in order of weight.

The specified `sha256_hex(...).substr(0, 16)` keeps 64 bits and discards the
rest, so a 64-bit hash is exactly as strong as what §9.1 asked for. Truncation,
not the algorithm, sets the collision bound here.

There is no cryptographic dependency in `conanfile.py`, and `core` cannot use
`QCryptographicHash` because it must stay Qt-free. SHA-256 would therefore mean
vendoring an implementation into the repository — which, under the
organisation's handling rules, is source for a cryptographic implementation and
is flagged for Security review before proceeding. That is a review this feature
does not otherwise need, bought for a property it does not use.

The ID is a lookup key for a user's own settings. Nothing authenticates against
it, and a user who edits their settings file can already point an override
wherever they like. Collision resistance against an adversary is not in the
threat model; collision resistance against accident is, and 64 bits gives a
birthday bound far beyond any realistic slide library.

The header comment states the non-cryptographic choice explicitly so that Stage
3 does not mistake an annotation file's embedded ID for proof of provenance.

## 2. `core` — the override record and its store

```cpp
struct ColorProfileOverride
{
    std::string slideId;            ///< computeSlideId result
    std::string profilePath;        ///< absolute path to the .icc/.icm file
    std::string slideDisplayName;   ///< last-known filename, for the dialog only
    bool displacedEmbedded = false; ///< set over a slide that embeds a profile
};

class IColorProfileOverrideStore
{
public:
    virtual ~IColorProfileOverrideStore() = default;
    virtual std::optional<ColorProfileOverride> find(const std::string& slideId) const = 0;
    virtual void set(const ColorProfileOverride& entry) = 0;
    virtual void remove(const std::string& slideId) = 0;
    virtual std::vector<ColorProfileOverride> all() const = 0;
};
```

Declared beside the existing `ISlideSource` and `ITileCache`, so the precedence
logic is testable against a fake with no `QSettings` involved.

`slideDisplayName` exists for the manage dialog alone — a table of bare 16-hex
IDs would be unusable. It is never matched against, so a renamed slide shows a
stale name in the dialog rather than losing its override.

`displacedEmbedded` is stored rather than recomputed because it records what the
user was warned about when they made the choice.

## 3. `core` — what the adapter is handed

The adapter must distinguish a default from an override, because the two differ
precisely in whether they may displace an embedded profile.

```cpp
struct SuppliedColorProfile
{
    std::vector<uint8_t> bytes;
    bool isSlideOverride = false; ///< true: displaces an embedded profile
};
```

This replaces the bare `std::vector<uint8_t> defaultProfileBytes` parameter on
both `SlideIOAdapter` constructors and on `buildManagedScene`.

## 4. `core` — `SlideInfo` addition

`ColorProfileSource` mirrors `slideio::ColorProfileSource` and must keep doing
so, and both a default and an override reach SlideIO as `Supplied`. The panel
therefore cannot tell them apart from that enum, and a viewer-level field is
added instead:

```cpp
/// Where the profile in use came from, as the viewer sees it. Distinct from
/// ColorProfileSource, which mirrors the library and cannot express this.
enum class ColorProfileOrigin { Library, DefaultSetting, SlideOverride };
```

`SlideInfo` gains `ColorProfileOrigin colorProfileOrigin = ColorProfileOrigin::Library;`
and `bool displacedEmbeddedProfile = false;` beside the existing
`colorProfileInfo`.

## 5. `infra` — the store implementation

`QSettingsColorProfileOverrideStore` implements `IColorProfileOverrideStore`
against `QSettings`, under `color/slideProfiles/<slideId>`, each entry holding
`profilePath`, `slideDisplayName` and `displacedEmbedded`.

**This adds Qt to a layer that currently has none.** `src/infra/CMakeLists.txt`
links `slideio-viewer-core`, `SlideIO::*` and `spdlog` only — `Qt6::Core` is
*not* among them, notwithstanding the target table in `CLAUDE.md`, which
describes the intended architecture rather than the built one. Implementation
adds `Qt6::Core` to that target, and adds the Qt bin directory to the
`infra-tests` `ENVIRONMENT_MODIFICATION` in `tests/CMakeLists.txt`, which today
prepends only the SlideIO directory and would leave the test binary unable to
find the Qt DLLs on Windows.

The store takes an explicit settings file in tests so they never touch the
user's real configuration:

```cpp
QSettingsColorProfileOverrideStore();                      // the application's settings
explicit QSettingsColorProfileOverrideStore(const QString& iniFilePath); // tests
```

**No automatic pruning.** An entry is a few hundred bytes; ten thousand is about
2 MB of settings, and no realistic library approaches that. Every entry is a
deliberate choice the user made about one slide, and evicting one silently would
discard that choice without telling them. Removal is explicit, through the
manage dialog.

## 6. `infra` — `SlideIOAdapter`

One condition changes. `buildManagedScene` currently reads:

```cpp
if (haveDefault && !info.colorProfileInfo.present) {
    cm.setSourceProfileOverride(::slideio::ColorProfile(defaultProfileBytes));
}
```

It becomes:

```cpp
if (haveBytes && (supplied.isSlideOverride || !info.colorProfileInfo.present)) {
    cm.setSourceProfileOverride(::slideio::ColorProfile(supplied.bytes));
}
```

The existing comment above it stays true and is extended to say that an override
may displace, and why the two cases differ. `setSourceProfileOverride` already
supports displacement; only this condition prevented it.

Nothing else in the tile path changes. `TileKey`'s `ColorMode` keeps its two
values, cache identity is untouched, and the reopen that applies an override
discards the old adapter and its tiles anyway.

## 7. `ui` — resolving the profile at open

Resolution happens on the background open thread, because that is where the
adapter is built. It must therefore touch neither `QSettings` nor any MainWindow
state, both of which belong to the UI thread.

`ViewportWidget::setDefaultColorProfile(std::vector<uint8_t>)` is replaced by a
snapshot of the whole policy, which MainWindow rebuilds on the UI thread
whenever the default or the override set changes, and which the open threads
capture by value exactly as they capture `defaultProfileBytes` today:

```cpp
struct ColorProfilePolicy
{
    std::vector<uint8_t> defaultBytes;  ///< already validated; empty if none
    std::unordered_map<std::string, std::string> overridePathsBySlideId;
};

void ViewportWidget::setColorProfilePolicy(ColorProfilePolicy policy);
```

Inside the worker, resolution is pure computation plus one file read:

1. `computeSlideId(scenes, fileSizeBytes)`.
2. Look the ID up in `overridePathsBySlideId`. No entry: use
   `SuppliedColorProfile{defaultBytes, false}` — today's behaviour exactly.
3. An entry: read the file and validate it with `inspectIccHeader` /
   `classifyDefaultProfile`. Good, use `SuppliedColorProfile{bytes, true}`.
   Bad, fall back to `{defaultBytes, false}` and record the problem text.

`SceneOpenResult` gains `std::string colorProfileProblem`, carried back to the
UI thread by the existing `installSceneOpenResult` path and surfaced the way
`defaultProfileProblem` already is. Reading an ICC file is a few hundred
kilobytes at most and happens once per open, off the UI thread.

Holding override paths rather than bytes keeps the snapshot small when a user
has many overrides, and means a profile edited on disk takes effect at the next
slide open rather than at the next restart.

## 8. `ui` — menu and behaviour

Three actions in the View menu, beside the existing default-profile pair:

| Action | Enabled when |
|--------|--------------|
| Set ICC Profile for This Slide… | a slide is open |
| Clear ICC Profile for This Slide | a slide is open and has an override |
| Manage Slide ICC Profiles… | always |

### Setting an override

1. File dialog, reusing the filter from `chooseDefaultProfile`.
2. Read the bytes; validate with `inspectIccHeader` and
   `classifyDefaultProfile`. Anything but `Ok` is rejected with the existing
   message text, so the two paths stay consistent.
3. If `info.colorProfileInfo.present`, warn before storing:

   > This slide embeds its own color profile (*name*). Overriding it displays
   > the slide through a profile the scanner did not produce. Continue?

   Cancel abandons the change. Continue sets `displacedEmbedded = true`.
4. Store the entry under `computeSlideId`.
5. Switch Color Management on if it is off. An override has no effect in
   `ColorMode::Raw`, and a menu action that visibly does nothing reads as a bug.
6. Reopen the current scene via `openScene(filePath, sceneIndex, driverId)`, so
   the change is visible at once.

### Clearing an override

Remove the entry, reopen the scene, fall back to embedded-or-default. Colour
management stays on; that toggle remains the user's.

### Manage dialog

A table over `store.all()`: slide name, profile path, status (OK / file missing
/ not a profile, from the same classification used at open), and a marker on
rows where `displacedEmbedded` is set. Buttons: Remove, Remove All. Removal
affects stored settings only; a slide currently open is reopened if its own
entry was removed.

## 9. Error handling

A stored override path can go stale exactly as the default can, and is treated
the same way: revalidated on every load, never handed to SlideIO when bad, and
the reason surfaced through the channel `defaultProfileProblem` already uses
(`MainWindow.cpp:405`) rather than letting SlideIO report that the *slide*
embeds no profile.

A bad override is **not** deleted. A profile on a disconnected network share
comes back, and discarding a deliberate choice because a file was briefly
unreachable is worse than showing it as broken. It appears in the manage dialog
with its status, and the user removes it if they mean to.

Fallback on a bad override follows the precedence list: embedded, then default,
then nothing — with the problem text visible, so the colours on screen are never
silently different from what the user asked for.

## 10. Testing

**`core`, no Qt:**

- `computeSlideId` is stable across repeated calls on equal input.
- It changes when any one input field changes — one case per field, including
  the geometry of a scene other than the first.
- It is independent of `filePath`.
- **It is identical for every scene of the same multi-scene file**: build the
  `SlideInfo` as each scene in turn would produce it, varying the open-scene
  fields the hash excludes, and assert one ID. This is the test that holds the
  per-scene non-goal honest.
- Scene order is not a free variable: a `scenes` vector differing only in
  ordering is rejected by construction, since each entry contributes its own
  `index`.
- No pair collides across a fixture table of realistic `SlideInfo` values.
- The two overloads agree: `computeSlideId(info, size)` equals
  `computeSlideId(info.scenes, size)` for the same inputs. This is what makes a
  pre-open identity and a post-open identity the same string.
- An empty `scenes` vector yields a stable ID rather than throwing or returning
  an empty string — `enumerateScenes` returns empty on a file it cannot read,
  and a lookup miss is the correct outcome, not a crash during open.
- Precedence resolution, against a fake `IColorProfileOverrideStore`: override
  beats embedded beats default beats nothing, with a bad override falling
  through to embedded while reporting its problem.

**`infra`:**

- `QSettingsColorProfileOverrideStore` round-trips every field, and `all()`
  returns what was stored.
- `remove` deletes one entry and leaves the rest.
- Stale-path classification matches the default-profile path's verdicts.
- `ColorManagedAdapterTest.cpp` gains the displacement case: a slide that embeds
  a profile, opened with `isSlideOverride = true`, reads pixels converted
  through the supplied profile rather than the embedded one — against the image
  corpus that test already uses.

**`ui`:**

- The warning appears only when the slide embeds a profile, and cancelling it
  stores nothing.
- Setting an override on a slide with colour management off turns it on.

## 11. Sequencing

1. `core`: `computeSlideId` with its tests.
2. `core`: `ColorProfileOverride`, `IColorProfileOverrideStore`,
   `SuppliedColorProfile`, `ColorProfileOrigin`, `SlideInfo` fields.
3. `infra`: `Qt6::Core` on the target, the Qt bin directory on the `infra-tests`
   PATH, and `QSettingsColorProfileOverrideStore` with its tests.
4. `infra`: the `buildManagedScene` condition and the constructor signatures.
5. `ui`: `ColorProfilePolicy`, `setColorProfilePolicy`, the worker-thread
   resolution, the `enumerateScenes` reordering in `openSlide`, and
   `reopenCurrentScene`.
6. `ui`: set and clear actions, the warning, the colour-management auto-enable.
7. `ui`: properties panel origin rows.
8. `ui`: manage dialog.
9. Update `03-software-architecture-and-design.md` §9.1 to the implemented
   identity scheme, and the colour management design's "No per-slide profile
   override" non-goal, which this feature retires.

Steps 1-5 are invisible to the user; step 6 is the first that changes
behaviour. Step 5 is the riskiest: it reorders work inside the open path that
every slide takes, so its regression surface is every format, not only the ones
with profiles.
