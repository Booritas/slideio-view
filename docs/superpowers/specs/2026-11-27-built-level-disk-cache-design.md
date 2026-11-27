# Disk cache for built pyramid levels

Status: design, 2026-11-27. Not implemented. Builds on
`2026-11-27-built-pyramid-levels-design.md` (approach C there, deferred until now).

## 1. Why

A slide without a usable overview level gets built levels (resident levels held
in memory, filled by one background pass over its coarsest stored level). On the
example slide, `PIT_01_00248_01.tiff` (63803×44997), that pass takes about a
minute in Raw mode and about 93 s colour-managed, and it is repeated **every time
the slide is opened**, and again on every colour toggle. A reader who reopens the
same slides through the day pays it every time.

The owner asked for the built tiles to be cached on disk: optional, **off by
default**, with a configurable folder and a retention time in days.

## 2. Goals, non-goals, anti-goals

Goals:

- With the cache on, the second open of a slide in the same colour rendition
  shows its built levels as fast as they can be read from disk (target: under
  2 s for the example slide's ~180 MB of resident levels), and the minimap
  overview appears with them.
- Off by default. Turning it on, changing the folder or the retention never
  affects a slide already open.
- Entries not used for longer than the retention time are deleted.

Non-goals:

- Caching slides that had an **open-time overview**. Since the built-levels
  review (F4) an ordinary pyramidal SVS whose coarsest level is over 2048 px
  also builds one small resident level, in 138–448 ms on the corpus. Caching it
  would write slide-derived pixels to disk to save a fraction of a second, so
  the cache applies only to slides that opened **without** an open-time
  overview — the slides this feature exists for (§5.5).
- Caching slides that are not one regular file. A directory slide (DICOM) or a
  format that keeps pixels in sidecar files (MRXS, VSI, AFI) can change without
  the opened path's size or modification time changing, so the key could not
  see it. The cache applies only when the opened path is a regular file and the
  driver is a single-file format (§5.5).
- Caching **virtual** built levels or full-resolution tiles. Only the resident
  levels the background pass creates are cached — they are what costs a minute
  to make. A virtual tile is one SlideIO read and stays one.
- A size cap on the cache folder. Retention is the only eviction the owner asked
  for; a cap can be added later on the same entry metadata.
- Sharing a cache between machines or users; a network folder is allowed but
  not designed for (two instances are handled, §5.4, nothing more).
- A "clear cache" button. Deleting the folder's contents by hand is safe (§5.4);
  a button is a later convenience.

Anti-goals — what would make this a failure even if it works:

- **Showing one slide's pixels for another.** The existing slide ID is FNV-1a
  over file size and scene geometry (`SlideId.h`), meant for keying the user's
  own settings. Two different scans with the same size and geometry would share
  it, and a cache keyed on it alone would show the wrong tissue — silently. The
  key must be stronger (§5.2), and a mismatch must fall back to building, never
  to showing.
- **Showing stale pixels** after the file changed in place, or in the wrong
  colour rendition or profile.
- **A cache that changes the pixels.** Built levels are exact box averages; the
  cache must be lossless.
- **A cache whose entries nobody can trace back to a slide.** An entry names
  the slide it came from (§5.2), so a person inspecting the folder can tell
  what is in it; see §6.
- **Making the open slower or fragile** when the cache is off, missing, corrupt,
  full or unwritable. Every cache failure degrades to building, with one log line.

## 3. Approaches

**A. Raw files.** One file per resident level holding the raster as the
accumulator holds it, plus a small header. Simplest and fastest to read; the
example slide's entry is ~180 MB per rendition.

**B. Lossless compressed files.** As A, but each level is deflate-compressed
(zlib via Qt's `qCompress`, level 1) in row blocks. H&E brightfield typically
compresses 2–3× losslessly, at a decode cost well under the disk read it saves
on most disks. Same exactness as A.

**C. A pyramidal TIFF** written through SlideIO's converter, reusable by other
tools. Heaviest: a TIFF writer in the viewer's build path, a format whose
reading goes back through SlideIO, and a file that looks like a slide and could
be mistaken for one.

**Chosen: B.** Lossless is non-negotiable (anti-goal 3), which rules out JPEG in
C's usual form; between A and B, B costs one compression call per block and
roughly halves the disk footprint a retention-only cache accumulates. A is the
fallback if measurement shows decompression dominating.

## 4. What the user sees

Preferences gains a group, **Built level cache**:

```
[ ] Cache built levels on disk
    Folder:     [ C:\Users\<user>\AppData\Local\SlideIO\SlideIO Viewer\level-cache ] [Browse...]
    Keep for:   [ 30 ] days since last use
```

- Off by default. The folder and the days fields are disabled while it is off.
- Default folder: `<AppLocalDataLocation>/level-cache` (the same root the log
  folder uses). An empty folder field after trimming means the default, as the
  annotation workspace field does.
- Days: 1–365, default 30.
- A slide opened from the cache logs one info line (`built levels loaded from
  cache in N ms`); nothing else in the UI changes. The minimap shows the
  overview as soon as the levels are loaded, by the same path a finished build
  uses.

## 5. The design

### 5.1 Settings (ui)

`ui::BuiltLevelCacheSettings { bool enabled; QString folder; int retentionDays; }`,
read and written through the application's `QSettings` under `levelCache/`
(`enabled`, `folder`, `retentionDays`), with the clamping and default rules as a
pure function so ui-tests reach them. Like `AnnotationSettings`' workspace, the
instance reads the store once at startup and changes only through the
Preferences dialog; a change applies to the **next** slide opened.
`MainWindow` keeps that copy as a member: the dialog is fed the member, not a
fresh read of the shared store, and its OK updates the member and writes the
store.

### 5.2 The cache key (core, pure)

An entry is identified by a key computed from everything that determines its
pixels:

- the existing slide ID, the scene index;
- the driver ID the slide was opened through, since a file opened through a
  forced different driver can decode differently;
- the file's size **and last-modified time** (both from the file system at
  open), so a file changed in place gets a new key;
- the source level's index, width, height, channel count and data type, and the
  resident factors — the plan that produced the levels;
- the colour mode, and for a managed rendition a fingerprint of the bound ICC
  profile's bytes (FNV-1a over the bytes, from a new
  `SlideIOAdapter::boundColorProfileFingerprint()`); a managed build with no
  obtainable fingerprint is **not cached**;
- the SlideIO library version (`slideio::getVersion()` or the
  `SLIDEIO_VERSION` the build links), and the colour transform's target and
  rendering intent, so a decoder or colour-engine upgrade that changes pixels
  invalidates the entry. The cost — every entry misses once after an upgrade —
  is accepted: a stale-pixel risk renewed on every hit is not;
- a cache format version.

`core::builtLevelCacheKey(...)` hashes these into a 32-hex-character directory
name (two independent 64-bit FNV-1a passes with different offsets; not
cryptographic, and not relied on alone — see below). The same fields are stored
verbatim in the entry's header and **compared field by field on load**; any
difference is a miss. The hash only names the directory; the header comparison
is what prevents a collision from showing the wrong slide.

The header also **describes** the slide, for whoever inspects the folder and
for the log lines that report a hit, miss or save: the opened path and file
name, the driver ID (also a key field), the scene index and name, the slide's level-0 size,
magnification and pixel size, the file size and modification time in readable
form, and when the entry was written. These descriptive fields are **not** part
of the key and are **not** compared on load, so a slide that was moved or
renamed still hits; the comparison is over the key fields above only. The
slide's raw metadata text is not copied (an OME-XML block can be megabytes and
adds nothing a person needs to recognise the slide). The owner's decision,
2026-11-27: the viewer runs in a secure environment, so paths and metadata need
not be kept out of the cache.

### 5.3 The store (infra)

`infra::BuiltLevelDiskCache` behind a small interface
`core::IBuiltLevelStore` (`load(key fields) -> optional<rasters>`,
`save(key fields, rasters)`, `touch`), injected into `BuiltPyramidSource`
(nullable: null means caching is off, and the source behaves exactly as today).

Entry layout: `<folder>/<key>/header.json` and `<folder>/<key>/level-<factor>.bin`
per resident level, each level stored as row blocks of deflate-compressed rows
with a per-block CRC-32 and the uncompressed size, so a truncated or bit-rotted
file is detected rather than displayed.

- **Load** (worker thread, before any band is read): read and validate the
  header, then each level; any failure — missing file, header mismatch, size
  mismatch, CRC failure, decompression error — is a miss, logged once at info,
  and the build runs as today. A hit fills the accumulator's resident levels as
  complete (a new accumulator entry point taking finished rasters), marks the
  build complete and notifies, exactly as a finished build does. The entry's
  `lastUsed` (a file in the entry, or the header's mtime) is touched. The load
  polls the build's cancel flag between blocks, so a close or colour toggle
  during it does not wait for the whole read; a cancelled load is not a miss,
  leaves the entry in place and logs nothing above debug. Before a block is
  decompressed, its 4-byte big-endian length prefix must equal the block
  header's uncompressed size, or the entry is a miss.
- **Save** (worker thread, after a build completes with **no failed rows** and
  was not cancelled or abandoned): write into `<folder>/<key>.tmp-<random>/`,
  then rename to `<folder>/<key>/`. If the rename fails because another
  instance won the race, delete the temporary directory. A failed write (disk
  full, permissions) is logged once and the temporary directory removed.
  Builds with failed rows, abandoned builds (unreliable source level) and
  cancelled builds are never saved.
- Saving runs after the completion notification, so the user sees the slide
  before the write starts; closing the slide during a save cancels it and
  removes the temporary directory.

### 5.4 Retention (core rule, infra sweep)

`core::expiredCacheEntries(entries, now, retentionDays)` decides; the sweep in
infra applies it. An entry expires when its last use is older than the
retention time. The sweep runs on a background thread at application start and
after every Preferences OK, over the configured folder **whether or not the
cache is enabled** (the sweep never creates a folder, so a cache that is off
still creates nothing); when Preferences changes the folder, the previous
folder is swept once as well. It enumerates only directories whose names look like
keys (and `*.tmp-*` directories older than a day, left by a crash), and deletes
those. It never deletes anything else in the folder, so pointing the cache at a
folder with other content is safe. Deleting entries by hand while the viewer
runs is safe: a missing file is a miss.

Two instances may share a folder: writes are rename-atomic, readers validate,
and a sweep deleting an entry another instance is reading makes that read a
miss (on Windows the delete of an open file fails and is retried at the next
sweep).

### 5.5 Wiring (ui)

`openSceneSync` (`ViewportWidget.cpp:1203`) receives the path but not the slide
ID: its callers compute the ID, one before the call (`:2347`) and two after it
(`:2620`, `:2751`). The ID is therefore passed **into** `openSceneSync`, with
the two later paths moved to compute it first. An empty ID (the size could not
be determined) means no caching. `openSceneSync` then reads the opened path's
size and modification time, and passes the store to `BuiltPyramidSource` only
when all of these hold, null otherwise:

- caching is enabled;
- the path is a regular file and the driver is a single-file format — an
  allowlist kept beside the cache code (`GDAL`, `SVS`, `PHTIFF`, `NDPI`,
  `QPTIFF`, `SCN`, `BIF`, `CZI`, `ZVI`, `LIF`, `ND2`), so a driver
  added later is excluded until someone decides. `OMETIFF` was on it and was
  removed in review (2026-11-27): SlideIO's OME-TIFF driver opens the files a
  `TiffData` names by `UUID/@FileName`, and the key's file size and
  modification time describe only the file opened;
- the slide ID is not empty;
- the open produced no open-time overview: the open-time pass returned no
  thumbnail, the same signal that decides whether a built overview is
  published (not `SlideInfo::overviewAvailable`, which can disagree with it). A colour toggle restarts the build in the new mode as
today; with the cache on, the restart first tries that rendition's entry, so
toggling back and forth after both renditions are cached costs two disk loads.

## 6. Data protection

Built levels are derived image data of the slide. On the example slide the
finest cached level is 1/8 of full resolution, but on a smaller slide every
built level can be resident: the 11963×6985 TIFF of check AN9 caches a level at
**1/2 of full resolution**, which is diagnostic-quality image data. A
slide may be patient-linked, so a cache entry may be **PHI under HIPAA** and
personal data under **GDPR**; the cache is data at rest that outlives the
session. The design keeps that to what the feature needs:

- **Off by default**; enabling it is a user decision in Preferences. Turning
  the cache off does not delete entries; they are deleted when their retention
  runs out.
- **What an entry holds**: the built levels' pixels, the key fields, and the
  descriptive fields of §5.2 (path, file name, driver, scene, size,
  calibration). It holds no label or macro image, no raw metadata text and no
  annotation; associated images are never built and never cached.
- **Storage limitation**: retention deletes entries after the configured number
  of days of disuse (GDPR art. 5(1)(e)); the default folder is under the user's
  own local application data, readable by that user only by default on Windows.
- **Not encrypted, and paths and metadata not withheld — the owner's decisions,
  2026-11-27**, on the grounds that the viewer is installed in a secure
  environment. The HIPAA Security Rule's device-and-media and access-control
  safeguards (§164.310(d), §164.312(a)) and GDPR's storage limitation are then
  met by that environment and by retention, not by the cache itself; a
  deployment outside such an environment should revisit both decisions.

## 7. Tests

core-tests:
- `builtLevelCacheKey`: stable for equal inputs; changes when any one field
  changes (slide ID, scene, size, mtime, each plan field, mode, profile
  fingerprint, format version); 32 lowercase hex characters.
- Header field comparison: equal → hit; any single field different → miss.
- `expiredCacheEntries`: exactly-at-boundary, one second over, future
  timestamps (clock skew) never expire, `*.tmp-*` older than a day expire,
  non-key names never listed.
- Accumulator: loading finished rasters marks every resident level complete and
  `extract` returns them byte for byte; wrong sizes are rejected.

infra-tests (temporary directory, fake source):
- Save then load round-trips every resident level byte for byte, for Byte,
  UInt16 and Float32 data.
- A hit reads no band from the source and notifies completion once.
- Truncated file, flipped byte (CRC), wrong header field, missing level file:
  each is a miss, the build runs, nothing wrong is served.
- A build with a failed band, an abandoned build and a cancelled build are not
  saved; no temporary directory is left behind.
- Two stores racing to save the same key leave exactly one valid entry.
- The sweep deletes expired key directories and stale temporaries and nothing
  else in the folder.
- Null store: behaviour identical to today (existing tests unchanged).

ui-tests: settings defaults, clamping (days 0 → 1, 1000 → 365, empty folder →
default) and round trip through a temporary `QSettings`.

## 8. Manual checks (group AO)

- AO1 Cache off (default): opening the example slide twice builds twice; the
  cache folder is not created.
- AO2 Cache on: first open builds and then writes an entry (log); second open
  shows the zoomed-out view and the minimap within ~2 s, no band reads in the
  log.
- AO3 Toggle colour management after both renditions are cached: each toggle
  loads from cache.
- AO4 Touch the slide file (change its mtime): the next open misses and builds.
- AO5 Corrupt one level file by hand: the next open logs a miss and builds; the
  view is correct.
- AO6 Close the slide while it is being saved: it closes at once; no `*.tmp-*`
  directory remains.
- AO7 Set retention to 1 day, set an entry's last use back two days (by hand),
  restart: the entry is deleted; other files in the folder are untouched.
- AO8 Point the folder at an unwritable location: the slide opens and builds
  normally; one warning in the log; nothing else.
- AO9 Inspect an entry: its header names the opened path, file name, driver
  and scene; move the slide file to another folder and reopen it: the entry
  still hits, and the header keeps the path it was written with.
- AO10 Disk usage of the example slide's entry, recorded (Raw and Managed).

## 9. Left to the builder

- Names of types and files, within the conventions.
- Row-block size for compression (a few hundred KB uncompressed is a sensible
  start) and the header's exact JSON layout (nlohmann is core-PRIVATE; infra may
  use Qt's JSON).
- Whether `lastUsed` is a separate small file or the header's modification time.

## 10. Results

| Check | Result | Notes |
| --- | --- | --- |
| AO1 | pass (smoke, 2026-11-27) | cache off, a scratch-installed build opened PIT_01_00248_01.tiff, no cache log lines, no level-cache folder created |
| AO2 | not run | |
| AO3 | not run | |
| AO4 | not run | |
| AO5 | not run | |
| AO6 | not run | |
| AO7 | not run | |
| AO8 | not run | |
| AO9 | not run | |
| AO10 | not run | |
