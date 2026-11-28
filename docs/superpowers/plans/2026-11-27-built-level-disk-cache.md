# Built-Level Disk Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optionally keep a slide's finished resident built levels on disk, so the next open of the same slide in the same colour mode loads them instead of rebuilding.

**Architecture:** Pure rules (key, header comparison, retention, eligibility, CRC) in core; a `core::IBuiltLevelStore` interface implemented by `infra::BuiltLevelDiskCache` (deflate row blocks + CRC-32, `header.json`, rename-atomic saves, a retention sweep); `infra::BuiltPyramidSource` tries the store before its band loop and saves after a clean build; ui owns the settings, the Preferences group and the wiring through `openSceneSync`.

**Tech Stack:** C++17, Qt 6 Core (`qCompress`, `QJsonDocument`, `QDir`, `QSaveFile` not needed), Catch2 v3, spdlog.

**Spec:** `docs/superpowers/specs/2026-11-27-built-level-disk-cache-design.md`

## Global Constraints

- Off by default. Settings live in `QSettings` under `levelCache/` with keys `enabled`, `folder`, `retentionDays`; retention clamps to **1–365**, default **30**; an empty folder means the default `<AppLocalDataLocation>/level-cache`. A settings change applies to the **next** slide opened.
- The cache is lossless: a hit must reproduce the built rasters byte for byte.
- Any cache failure (missing, truncated, CRC, header mismatch, unwritable folder, disk full) degrades to building, with **one** log line; nothing wrong is ever shown.
- Key fields (spec §5.2): slide ID, scene index, file size, file modification time, source level index/width/height, channel count, data type, resident factors, colour mode, managed profile fingerprint, rendition tag (transform target + intent), SlideIO version, cache format version. All stored in the header and **compared field by field on load**. The 32-hex directory name is two FNV-1a 64-bit passes with different offsets.
- Descriptive header fields (path, file name, driver, scene index and name, level-0 size, magnification, pixel size, file size, modification time, written-at) are **never** part of the key and **never** compared. Raw slide metadata text is never written.
- Cache only when: enabled; the opened path is a regular file; the driver is in `GDAL SVS PHTIFF NDPI OMETIFF QPTIFF SCN BIF CZI ZVI LIF ND2`; the slide ID is non-empty; the open produced **no** open-time overview. A Managed build with no obtainable profile fingerprint is not cached (Raw still is).
- Only complete builds with **no failed rows**, not cancelled, not abandoned, are saved. Saves go to `<folder>/<key>.tmp-<random>/` then a directory rename to `<folder>/<key>/`; a lost race deletes the temporary.
- The sweep deletes only key-named directories whose last use is more than `retentionDays × 86400` s ago (exactly at the boundary is kept; a future timestamp never expires) and `*.tmp-*` directories older than 86400 s. Nothing else in the folder is touched.
- Load and save run on the build's worker thread, never the UI thread. A close or colour toggle during a save cancels it and removes the temporary directory.
- Paths cross layers as UTF-8 `std::string`; infra converts with `QString::fromStdString` and uses `QDir`/`QFile` only (no `std::filesystem` on narrow strings).
- Conventions per CLAUDE.md: core is Qt-free; nlohmann stays core-PRIVATE (infra uses `QJsonDocument`); never the floating-point `std::to_chars`/`from_chars`; no test constructs a widget; only `core-tests` may be run directly — every other suite runs under `ctest`.
- Build and test: `cmake --build build/build --config Release`, then `ctest --test-dir build/build -C Release --output-on-failure` with `SLIDEIO_VIEWER_TEST_IMAGES=D:/Projects/slideio/images/images`.

## Review Focus

1. **A cache folder whose path holds non-ASCII characters** (a user name like `Jörg` under AppData) — saves and loads must round-trip; a narrow-string path API would silently miss every time. Pinned in Task 3.
2. **A level wider than one compression block** (wide Float32 multi-channel rows) — a block must hold at least one whole row, never split one. Pinned in Task 3 with a tiny block size.
3. **Close or colour toggle while a save is writing** — the UI must not wait for the whole write and no `*.tmp-*` may remain. Pinned in Task 4.
4. **Toggling colour management on a cached slide** — each mode has its own entry; a Raw entry must never answer a Managed load. Pinned in Task 4.
5. **Two viewer instances sharing a folder** — racing saves leave one valid entry; a sweep in one never deletes the other's fresh entry. Pinned in Task 3.

---

### Task 1: Core cache rules

**Files:**
- Create: `src/core/include/slideio/viewer/core/BuiltLevelCache.h`, `src/core/src/BuiltLevelCache.cpp`
- Create: `src/core/include/slideio/viewer/core/IBuiltLevelStore.h` (interface only)
- Modify: `src/core/CMakeLists.txt` (add the .cpp), `tests/CMakeLists.txt` (add the test to core-tests)
- Test: `tests/core/BuiltLevelCacheTest.cpp`

**Interfaces — Produces:**

```cpp
namespace slideio::viewer::core {
constexpr int kBuiltLevelCacheFormatVersion = 1;
constexpr int64_t kCacheTemporaryMaxAgeSeconds = 86400;

struct BuiltLevelCacheKey {
    std::string slideId; int sceneIndex = 0;
    uint64_t fileSize = 0; int64_t fileModifiedMs = 0;
    int sourceLevel = -1; int sourceWidth = 0; int sourceHeight = 0;
    int numChannels = 0; DataType dataType = DataType::None;
    std::vector<int> residentFactors;
    ColorMode colorMode = ColorMode::Raw;
    std::string profileFingerprint;   // empty for Raw
    std::string renditionTag;         // empty for Raw
    std::string slideioVersion;
    int formatVersion = kBuiltLevelCacheFormatVersion;
};
bool operator==(const BuiltLevelCacheKey&, const BuiltLevelCacheKey&);
bool operator!=(const BuiltLevelCacheKey&, const BuiltLevelCacheKey&);

// Everything about the slide the source cannot know: filled by ui at open.
struct BuiltLevelCacheIdentity {
    std::string slideId; int sceneIndex = 0; uint64_t fileSize = 0; int64_t fileModifiedMs = 0;
    std::string managedProfileFingerprint;  // empty: no managed rendition can be cached
    std::string managedRenditionTag;
    std::string slideioVersion;
};

// Never part of the key, never compared.
struct BuiltLevelCacheDescription {
    std::string path, fileName, driverId, sceneName;
    int sceneIndex = 0; int width = 0; int height = 0;
    double magnification = 0.0; double pixelSizeXUm = 0.0; double pixelSizeYUm = 0.0;
    uint64_t fileSize = 0; int64_t fileModifiedMs = 0;
};

// The key for one build; nullopt for a Managed build whose identity has no fingerprint.
std::optional<BuiltLevelCacheKey> builtLevelCacheKeyFor(const BuiltLevelCacheIdentity& identity,
    int sourceLevel, const LevelInfo& source, int numChannels, DataType dataType,
    const std::vector<int>& residentFactors, ColorMode mode);

std::string builtLevelCacheKeyName(const BuiltLevelCacheKey& key);   // 32 lowercase hex
std::string colorProfileFingerprint(const std::vector<uint8_t>& bytes); // 16 lowercase hex FNV-1a 64; "" for empty
uint32_t crc32(const uint8_t* data, size_t size);                    // IEEE 802.3, reflected, 0xEDB88320

bool isCacheKeyName(std::string_view name);         // exactly 32 [0-9a-f]
bool isCacheTemporaryName(std::string_view name);   // <key name>.tmp-<one or more chars>

struct CacheEntryFacts { std::string name; int64_t lastUsedSeconds = 0; };
// Names to delete: key names with now - lastUsed > retentionDays*86400; temporaries with
// now - lastUsed > kCacheTemporaryMaxAgeSeconds; any other name is never returned.
std::vector<std::string> expiredCacheEntries(const std::vector<CacheEntryFacts>& entries,
                                             int64_t nowSeconds, int retentionDays);

bool isSingleFileDriver(std::string_view driverId);  // the allowlist in Global Constraints, exact case
struct BuiltLevelCacheEligibility { bool enabled = false; bool regularFile = false; std::string driverId;
                                    std::string slideId; bool hadOpenTimeOverview = false; };
bool builtLevelCacheApplies(const BuiltLevelCacheEligibility& facts);
}
```

`IBuiltLevelStore.h`:

```cpp
struct BuiltLevelView { int width = 0; int height = 0; const uint8_t* data = nullptr; size_t size = 0; };
class IBuiltLevelStore {
public:
    virtual ~IBuiltLevelStore() = default;
    // Finest first. levelSizes are the expected {width, height}; each raster must be exactly
    // width*height*pixelBytes. nullopt on any miss (logged once by the store). Marks the entry used.
    virtual std::optional<std::vector<std::vector<uint8_t>>> load(const BuiltLevelCacheKey& key,
        const std::vector<std::pair<int, int>>& levelSizes, size_t pixelBytes) = 0;
    // Returns true when an entry now exists for the key. Polls `cancel` between blocks.
    virtual bool save(const BuiltLevelCacheKey& key, const BuiltLevelCacheDescription& description,
                      const std::vector<BuiltLevelView>& levels, const std::atomic<bool>& cancel) = 0;
};
```

- [ ] **Step 1: Write the failing tests** in `tests/core/BuiltLevelCacheTest.cpp`:
  - `builtLevelCacheKeyName is stable and 32 lowercase hex`: two equal keys give equal names; `std::all_of` over `[0-9a-f]`, length 32.
  - `builtLevelCacheKeyName changes with every key field`: a base key, then for each field (slideId, sceneIndex, fileSize, fileModifiedMs, sourceLevel, sourceWidth, sourceHeight, numChannels, dataType, residentFactors `{8,16}`→`{8,16,32}`, colorMode, profileFingerprint, renditionTag, slideioVersion, formatVersion) a copy with only that field changed: name differs AND `!=` holds.
  - `builtLevelCacheKeyFor`: Raw clears fingerprint and tag even when the identity carries them; Managed with empty `managedProfileFingerprint` → `nullopt`; Managed copies both.
  - `colorProfileFingerprint`: `""` for empty; 16 hex; differs for `{1,2,3}` vs `{1,2,4}`.
  - `crc32`: `"123456789"` → `0xCBF43926u`; empty → `0u`.
  - `isCacheKeyName` / `isCacheTemporaryName`: 32 lowercase hex true; 31 chars, upper case, `g` false; `<key>.tmp-ab12` true; `<key>.tmp-` false; `notes.txt` false for both.
  - `expiredCacheEntries`: retention 30, now = 10'000'000: key entry at `now - 30*86400` kept, at `now - 30*86400 - 1` returned, at `now + 1000` (future) kept; temporary at `now - 86400` kept, `now - 86401` returned; `notes.txt` at 0 never returned.
  - `builtLevelCacheApplies`: true for `{true, true, "GDAL", "abc", false}`; false when each one of the five facts is flipped (disabled, not a regular file, `"DCM"`, `""` slide ID, had overview); `isSingleFileDriver("gdal")` false (exact case).

- [ ] **Step 2: Run to verify they fail** — `cmake --build build/build --config Release` fails to compile on the missing header.

- [ ] **Step 3: Implement** `BuiltLevelCache.{h,cpp}` and `IBuiltLevelStore.h`. The key name hashes a canonical serialisation of every field (length-prefixed strings, fixed-width integers) with FNV-1a 64 from offset `0xcbf29ce484222325` and again from a second offset of your choosing, printed as two 16-hex halves. CRC-32 is a table-driven reflected implementation.

- [ ] **Step 4: Run** `build/build/Release/slideio-viewer-core-tests.exe "[BuiltLevelCache]"` (tag the tests so) — all pass; then the whole core suite passes.

- [ ] **Step 5: Commit** — `git commit -m "Core rules for the built-level disk cache"`.

---

### Task 2: Accumulator accepts finished levels

**Files:**
- Modify: `src/core/include/slideio/viewer/core/BuiltLevelAccumulator.h`, `src/core/src/BuiltLevelAccumulator.cpp`
- Test: `tests/core/BuiltLevelAccumulatorTest.cpp`

**Interfaces — Produces:**

```cpp
/// Replaces every resident level with finished rasters, finest first, each exactly
/// levelWidth(i)*levelHeight(i)*numChannels*dataTypeSize bytes, and marks the build
/// complete with every row Ready. Throws std::invalid_argument, touching nothing,
/// for the wrong level count or any wrong size, or once any chunk has been added.
void loadComplete(std::vector<std::vector<uint8_t>> levels);
/// True when any row of any resident level holds a failed box.
bool hasFailures() const;
/// The level's whole buffer, for saving; valid until the accumulator changes.
const std::vector<uint8_t>& levelBytes(int residentIndex) const;
```

- [ ] **Step 1: Failing tests** (tag `[BuiltLevelAccumulator]`):
  - `loadComplete restores what a build produced`: build a 100×60 Byte 3-channel source with factors `{2,4}` through `addChunk`; copy `levelBytes(0)`, `levelBytes(1)`; feed them to a fresh accumulator's `loadComplete`; `complete()` true, `regionState(i, 0, levelHeight(i))` Ready for both, `extract` of each whole level byte-equal to the original, `hasFailures()` false.
  - `loadComplete rejects wrong shapes`: one level instead of two; one byte short; after an `addChunk` — each throws `std::invalid_argument` and `complete()` stays false.
  - `hasFailures reports a failed chunk`: `failChunk` on one chunk of a band → true; a clean build → false.

- [ ] **Step 2: Run** core-tests — compile fails.
- [ ] **Step 3: Implement.** `loadComplete` sets each level's `completedRows = height`, clears failed spans, moves the buffers in, and sets the band cursor past the source so `complete()` is true.
- [ ] **Step 4: Run** core-tests — pass.
- [ ] **Step 5: Commit** — `"Let the built-level accumulator take finished levels"`.

---

### Task 3: `infra::BuiltLevelDiskCache`

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/BuiltLevelDiskCache.h`, `src/infra/src/BuiltLevelDiskCache.cpp`
- Modify: `src/infra/CMakeLists.txt`, `tests/CMakeLists.txt` (infra-tests)
- Test: `tests/infra/BuiltLevelDiskCacheTest.cpp`

**Interfaces — Consumes:** Task 1 (`IBuiltLevelStore`, key/name/CRC/expiry rules). **Produces:**

```cpp
class BuiltLevelDiskCache : public core::IBuiltLevelStore {
public:
    static constexpr size_t kDefaultBlockBytes = 256 * 1024;
    explicit BuiltLevelDiskCache(std::string folderUtf8, size_t blockBytes = kDefaultBlockBytes);
    const std::string& folder() const;
    std::optional<std::vector<std::vector<uint8_t>>> load(...) override;
    bool save(...) override;
    // Deletes expired entries and stale temporaries; returns how many were deleted.
    static int sweep(const std::string& folderUtf8, int retentionDays, int64_t nowSeconds);
};
```

Entry layout (exact names): `<folder>/<keyName>/header.json`, `level-<i>.bin` for resident index `i` (finest 0), `last-used` (ASCII decimal epoch seconds). `header.json` holds `"formatVersion"`, a `"key"` object with every `BuiltLevelCacheKey` field (factors as an array, data type and colour mode as integers, file size and mtime as decimal strings to avoid double rounding), a `"levels"` array of `{width, height, bytes}`, a `"description"` object with the description fields plus `"writtenAt"` (ISO-8601 UTC). `level-<i>.bin`: magic `SVBL`, `uint32` format version, `uint32` block count, then per block `uint32 uncompressedSize`, `uint32 compressedSize`, `uint32 crc32(uncompressed)`, then `qCompress(rows, 1)` bytes. All integers little-endian. A block is a whole number of rows totalling at most `blockBytes`, and **at least one row** whatever `blockBytes` is.

Load: missing folder/entry → miss with no log above debug; anything else wrong (header unreadable, any key field `!=`, level count/size mismatch, bad magic, block count short, CRC mismatch, decompressed size mismatch, file trailing bytes) → miss, **one** `spdlog::info` naming the key name, the reason and `description.path` from the header if readable. On a hit, rewrite `last-used`. Save: create `<folder>` if needed, write into `<folder>/<keyName>.tmp-<8 random hex>/` (`last-used` last), check `cancel` between blocks (cancelled → remove the temporary, return false), then `QDir::rename` to `<keyName>`; if that fails and a valid-looking `<keyName>/header.json` exists, remove the temporary and return true; any I/O failure → one `spdlog::warn`, temporary removed, return false. Sweep: list immediate subdirectories, `lastUsed` = `last-used` content if parseable else the directory's modification time, delete what `core::expiredCacheEntries` returns with `QDir::removeRecursively`; a failure to delete (file open elsewhere) is skipped silently and retried next sweep.

- [ ] **Step 1: Failing tests** (tag `[BuiltLevelDiskCache]`, each in its own `QTemporaryDir`):
  - `save then load round-trips` for Byte 3-channel, UInt16 1-channel and Float32 2-channel two-level rasters filled with a deterministic pattern: `load` returns byte-equal vectors.
  - `a non-ASCII folder round-trips`: folder `<tmp>/Jörg Ünïcode/cache` (UTF-8 in a `std::string`), same assertion.
  - `a block always holds whole rows`: `blockBytes = 1`, rows of 3000 bytes — round-trips; the `.bin` file has exactly `height` blocks.
  - `each corruption is a miss`: truncate `level-1.bin` by 10 bytes; flip one byte in the middle of `level-0.bin`; change `"sceneIndex"` in `header.json`; delete `level-1.bin`; ask with a key differing only in `slideioVersion` — each `load` returns `nullopt`.
  - `cancel leaves nothing behind`: `cancel` already true → `save` false, the folder contains no subdirectory.
  - `racing saves leave one valid entry`: two caches on one folder, both `save` the same key from two `std::thread`s → both return true, exactly one directory, `load` hits, no `.tmp-` left.
  - `an unwritable folder fails cleanly`: folder path is an existing regular **file** → `save` false, no exception.
  - `sweep`: entries with `last-used` at now−31 days and now−29 days (retention 30), a `.tmp-` at now−2 days, a file `notes.txt` and a directory `keep-me` both old → returns 2, only the 31-day entry and the temporary are gone.
  - `load marks the entry used`: write `last-used` = 1, `load` → content is now ≥ the test's start time.

- [ ] **Step 2: Run** — build fails.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `ctest --test-dir build/build -C Release -R infra-tests --output-on-failure` — pass.
- [ ] **Step 5: Commit** — `"Disk store for built pyramid levels"`.

---

### Task 4: The built-level source uses the store; the adapter names its rendition

**Files:**
- Modify: `src/infra/include/slideio/viewer/infra/BuiltPyramidSource.h`, `src/infra/src/BuiltPyramidSource.cpp`
- Modify: `src/infra/include/slideio/viewer/infra/SlideIOAdapter.h`, `src/infra/src/SlideIOAdapter.cpp`
- Test: `tests/infra/BuiltPyramidSourceTest.cpp`

**Interfaces — Consumes:** Tasks 1–3. **Produces:**

```cpp
// BuiltPyramidSource. Call before start(); a null store (the default) means no caching
// and behaviour exactly as before.
void setLevelStore(std::shared_ptr<core::IBuiltLevelStore> store,
                   core::BuiltLevelCacheIdentity identity,
                   core::BuiltLevelCacheDescription description);

// SlideIOAdapter.
// core::colorProfileFingerprint of the bytes the managed scene was bound to (a supplied
// profile when it was used, else the embedded one); "" when there is no managed scene.
std::string boundColorProfileFingerprint() const;
// "sRGB/<intent>" for the transform buildManagedScene sets up; one place names both.
static std::string managedRenditionTag();
```

Worker behaviour (`run`): before the band loop, when a store is set and `core::builtLevelCacheKeyFor(...)` gives a key, call `load` with the resident levels' sizes. On a hit: take the state lock exclusively, return if the generation moved, `m_accumulator->loadComplete(...)`, `m_complete = true`, unlock, log one info line (`"loaded built levels in mode {} from cache {} in {} ms"`), `notify(true)` unless cancelled, return. On a miss, build as today. After a build sets `m_complete` and has notified: save only when not cancelled, the source is not unreliable, `!m_accumulator->hasFailures()`, the generation is unchanged and a key exists; hold the state lock **shared** across `save` (a restart joins this worker before it takes the lock exclusively, so the buffers cannot move), pass `m_cancel` as the cancel flag, and log the outcome once. A stopped build (the catch path) never saves.

- [ ] **Step 1: Failing tests** (tag `[BuiltPyramidSource][cache]`), with an in-test `MemoryStore : core::IBuiltLevelStore` that records calls, keeps saved rasters by key name, can block inside `save` on a latch, and with the existing `FakeSource`:
  - `a clean build is saved once, then a second source hits without a band read`: source A builds and saves (`saves == 1`); source B over a fresh `FakeSource` with the same store and identity completes, its fake recorded **zero** `readBlock` calls, completion notified exactly once, and every resident tile is ready and byte-equal to A's.
  - `a null store changes nothing`: the existing behaviour test passes with `setLevelStore` never called (no new assertion needed beyond `saves == 0` on a store not attached).
  - `a build with a failed chunk is not saved`: the fake fails one `readBlock` → `saves == 0`.
  - `an abandoned build is not saved`: mark the source level unreliable mid-build → `saves == 0`.
  - `each colour mode has its own entry`: identity with a fingerprint and tag; build Raw, `setColorMode(Managed)`, wait for completion → two saved keys whose names differ and whose `colorMode` fields are Raw and Managed; a fresh source in Managed loads the Managed one.
  - `managed without a fingerprint is not cached`: identity fingerprint `""`, start in Managed → no `load`, no `save` calls.
  - `destruction during a save returns promptly and leaves no entry`: the store blocks in `save` until `cancel` is observed true; destroying the source completes within 2 s and the store reports the save cancelled.

- [ ] **Step 2: Run** — compile fails.
- [ ] **Step 3: Implement** the source changes, then the adapter's two methods: record the bound bytes' fingerprint in `buildManagedScene`'s caller when a managed scene is built; read the rendering intent `ColorManagementWrap` uses by default from `extern/slideio/src/slideio/transformer/colormanagementwrap.hpp` and spell the tag from it, so the key changes if that default ever does.
- [ ] **Step 4: Run** infra-tests under ctest — pass, including every pre-existing BuiltPyramidSource test.
- [ ] **Step 5: Commit** — `"Load and save built levels through the level store"`.

---

### Task 5: Settings, Preferences and wiring

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/BuiltLevelCacheSettings.h`, `src/ui/src/BuiltLevelCacheSettings.cpp`
- Modify: `src/ui/include/slideio/viewer/ui/PreferencesDialog.h`, `src/ui/src/PreferencesDialog.cpp`
- Modify: `src/ui/src/MainWindow.cpp`, `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, `src/ui/src/ViewportWidget.cpp`
- Modify: `tests/CMakeLists.txt` (ui-tests)
- Test: `tests/ui/BuiltLevelCacheSettingsTest.cpp`

**Interfaces — Consumes:** Tasks 1, 3, 4. **Produces:**

```cpp
namespace slideio::viewer::ui {
constexpr int kDefaultCacheRetentionDays = 30;
struct BuiltLevelCacheSettings { bool enabled = false; QString folder; int retentionDays = kDefaultCacheRetentionDays; };
QString defaultBuiltLevelCacheFolder();             // AppLocalDataLocation + "/level-cache"
QString resolveBuiltLevelCacheFolder(const QString& stored);   // trimmed-empty -> default
int clampCacheRetentionDays(int days);              // [1, 365]
BuiltLevelCacheSettings loadBuiltLevelCacheSettings(QSettings& settings);  // clamped; folder as stored
void saveBuiltLevelCacheSettings(QSettings& settings, const BuiltLevelCacheSettings& value);
}
// ViewportWidget: the store for slides opened from now on; null turns caching off.
void setBuiltLevelStore(std::shared_ptr<core::IBuiltLevelStore> store);
```

Wiring decisions:
- `openSceneSync` gains `const std::string& slideId` and `std::shared_ptr<core::IBuiltLevelStore> store` parameters. The two callers that compute the slide ID after the call (`ViewportWidget.cpp:2616-2620` scene open; the aux-image path at `:2751` does not use `openSceneSync` and is left alone) compute it first. The viewport hands `openSceneSync` a copy of its store pointer taken on the UI thread when the open starts.
- Inside `openSceneSync`, after `readCoarseLevelAndBuildThumbnail`: `QFileInfo` of the path gives `isFile()`, `size()` and `lastModified().toMSecsSinceEpoch()`; build `core::BuiltLevelCacheEligibility` with `hadOpenTimeOverview = r.slideInfo.overviewAvailable` and `driverId = r.slideInfo.driverId`; when `core::builtLevelCacheApplies` and the source `hasBuiltLevels()`, call `setLevelStore` with the identity (adapter's `boundColorProfileFingerprint()`, `SlideIOAdapter::managedRenditionTag()`, `infra::slideioLibraryVersion()`) and the description. Keep the adapter in a local before wrapping it so it can be asked.
- `MainWindow`: at startup read the settings with a default `QSettings`; when enabled, create `infra::BuiltLevelDiskCache(resolveBuiltLevelCacheFolder(folder).toStdString())`, hand it to the viewport, and start `BuiltLevelDiskCache::sweep` on `QThreadPool::globalInstance()`. After Preferences OK, save the settings, replace the viewport's store (null when disabled) and, when enabled, start a sweep.
- `PreferencesDialog` gains a "Built level cache" group: checkbox "Keep built levels on disk", folder `QLineEdit` + "Browse..." (placeholder shows the default folder), `QSpinBox` 1–365 with suffix " days" labelled "Keep for", folder and days disabled while unchecked. Tooltip on the group: "Speeds up reopening large slides that have no stored overview. Entries hold image pixels and the slide's file path; keep the folder on local, access-controlled storage." Constructor takes a `BuiltLevelCacheSettings`; a `builtLevelCacheSettings()` getter returns the edited values with days clamped.

- [ ] **Step 1: Failing tests** in `tests/ui/BuiltLevelCacheSettingsTest.cpp` (an INI `QSettings` in a `QTemporaryDir`): defaults when the store is empty (`enabled` false, folder `""`, days 30); `clampCacheRetentionDays(0) == 1`, `(1000) == 365`, `(-5) == 1`, `(45) == 45`; a stored `retentionDays = 0` loads as 1; `resolveBuiltLevelCacheFolder("   ")` equals `defaultBuiltLevelCacheFolder()`, which ends with `"/level-cache"`; save then load round-trips `{true, "D:/x/ç", 7}` under keys `levelCache/enabled`, `levelCache/folder`, `levelCache/retentionDays`.
- [ ] **Step 2: Run** — compile fails.
- [ ] **Step 3: Implement** settings, dialog, `MainWindow` and `ViewportWidget` wiring as above.
- [ ] **Step 4: Run** the full build and all four ctest suites — pass.
- [ ] **Step 5: Smoke** — install to a scratch prefix (`cmake --install build/build --config Release --prefix <scratchpad>/cache-smoke`), start it, confirm Preferences shows the group with the box unchecked, and that opening a slide with the cache off writes no `level-cache` folder (AO1). Record in the report.
- [ ] **Step 6: Commit** — `"Preferences and wiring for the built-level disk cache"`.

---

### Task 6: Documentation

**Files:**
- Modify: `CLAUDE.md` (a key-decision bullet after the built-levels one; add `BuiltLevelCache` to the core list and `BuiltLevelDiskCache` to the infra list in the architecture block)
- Modify: `docs/superpowers/specs/2026-11-27-built-level-disk-cache-design.md` (a §10 "Results" placeholder table for AO1–AO10, every row "not run")

- [ ] **Step 1:** Write the CLAUDE.md bullet: what is cached and when, the key and its field-by-field comparison, that descriptive fields are never compared, the eligibility rules, save-only-clean-builds, retention, and the owner's 2026-11-27 decisions (unencrypted; paths and metadata recorded because the deployment is a secure environment).
- [ ] **Step 2:** Add the §10 table to the spec.
- [ ] **Step 3: Commit** — `"Docs: built-level disk cache"`.

The manual checks AO2–AO10 are run by the controller after the final review, against an installed build, and recorded in §10.
