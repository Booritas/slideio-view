# Per-slide ICC Profile Override Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user nominate an ICC profile for one particular slide, remembered across sessions, taking precedence over both the global default profile and the profile the slide embeds.

**Architecture:** A new pure-C++ `computeSlideId` in `core` derives a 16-hex identity from file-level properties (file size and the geometry of every scene), so the same slide is recognised after a move or rename and from whichever scene is open. `core` declares `IColorProfileOverrideStore`; `infra` implements it over `QSettings`. At slide open, `MainWindow` hands `ViewportWidget` a `ColorProfilePolicy` snapshot, which the background open thread resolves into a `SuppliedColorProfile` before constructing the adapter — the one place a profile can still be chosen. A single condition in `buildManagedScene` lets an override, unlike a default, displace an embedded profile.

**Tech Stack:** C++17, Qt 6 Widgets, SlideIO (via `extern/slideio` submodule), Catch2 v3, CMake 3.20+, Conan 2, spdlog.

**Spec:** `docs/superpowers/specs/2026-10-07-per-slide-color-profile-design.md`

## Global Constraints

- `slideio-viewer-core` is **pure C++17 and Qt-free**. No `QString`, no `QSettings`, no Qt headers in any file under `src/core/`.
- Naming: classes `PascalCase`, functions `camelCase`, members `m_camelCase`, constants `kPascalCase`, files `PascalCase.h/.cpp`, namespaces lowercase (`slideio::viewer::core`).
- 4-space indent, 120-character line limit, Allman braces for classes and functions, K&R for control flow, `#pragma once` for header guards.
- Include order: own header, project headers, Qt headers, std headers.
- Every commit lands directly on `main`. No feature branch, no PR.
- All AI-generated code must be human-reviewed before commit — do not push unreviewed work.
- Build: `export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"` then `./build.sh`. `conan.exe` is not on the default PATH.
- The build tree executable will not start; `./build.sh` already runs `cmake --install`, so run the installed binary for any manual check.
- Tests: `ctest --test-dir build/build -C Release --output-on-failure`. A single suite: add `-R core-tests` (or `infra-tests`, `ui-tests`).
- `SLIDEIO_VIEWER_TEST_IMAGES` points at the image corpus, which is not in this repository. Tests needing it must skip cleanly when it is unset.

## Review Focus

These are the failure modes the spec implies but does not give tests for. Each line names the condition and the behaviour a reasonable user expects; each has a test pinned to the task that owns the code.

1. **`enumerateScenes` fails** (corrupt, locked, or truncated file) and returns an empty scene vector. Every unreadable file would then hash to one identity, and an override stored for slide A would be applied to unrelated slide B. Expected: an empty scene vector is never used for an override lookup at all. → Task 5.
2. **The override profile lives on an unreachable network share.** Reading it happens on the open thread, so a blocking share would stall the slide open behind a filesystem timeout. Expected: the slide still opens, through the default or embedded profile, with the problem reported. → Task 5.
3. **The slide file is modified in place** (re-saved, converted, re-exported) so its size changes. The identity changes and the override silently stops applying. Expected: the override simply does not match — never a crash, never a wrong match. → Task 1.
4. **Two genuinely different slides share a file size and scene geometry.** They collide onto one identity, and one slide's override applies to the other. Expected: documented and bounded, with the collision surface pinned by a test so it cannot silently widen. → Task 1.
5. **An override is set or cleared while an open is already in flight.** The reopen races the pending open. Expected: the most recent open wins, which is what the existing `openOpId` guard already enforces — the new reopen path must go through it rather than around it. → Task 5, Step 9. This one is held by construction rather than by a unit test: `reopenCurrentScene` delegates to `openScene` / `openAuxImage`, which bump `openOpId`, and a test would have to drive two overlapping async opens through a live GL widget. The step verifies it by hand instead, and says so rather than implying coverage that does not exist.

---

### Task 1: `core` — content-derived slide identity

**Files:**
- Create: `src/core/include/slideio/viewer/core/SlideId.h`
- Create: `src/core/src/SlideId.cpp`
- Test: `tests/core/SlideIdTest.cpp`
- Modify: `src/core/CMakeLists.txt` (add `src/SlideId.cpp` to the source list)
- Modify: `tests/CMakeLists.txt` (add `core/SlideIdTest.cpp` to `slideio-viewer-core-tests`)

**Interfaces:**
- Consumes: `slideio::viewer::core::SceneInfo` and `SlideInfo` from `Types.h` (existing).
- Produces: `std::string computeSlideId(const std::vector<SceneInfo>&, uint64_t)` and `std::string computeSlideId(const SlideInfo&, uint64_t)`. Tasks 5 and 6 call both.

- [ ] **Step 1: Write the failing test**

Create `tests/core/SlideIdTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/SlideId.h"
#include "slideio/viewer/core/Types.h"

#include <set>
#include <string>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

SceneInfo makeScene(int index, int width, int height, int numChannels)
{
    SceneInfo s;
    s.index = index;
    s.width = width;
    s.height = height;
    s.numChannels = numChannels;
    return s;
}

std::vector<SceneInfo> twoScenes()
{
    return {makeScene(0, 100000, 80000, 3), makeScene(1, 2048, 1024, 3)};
}

} // namespace

TEST_CASE("computeSlideId is stable and well-formed", "[core][SlideId]")
{
    const std::string id = computeSlideId(twoScenes(), 4096);

    REQUIRE(id.size() == 16);
    REQUIRE(id == computeSlideId(twoScenes(), 4096));
    REQUIRE(id.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST_CASE("computeSlideId changes when any input changes", "[core][SlideId]")
{
    const std::string base = computeSlideId(twoScenes(), 4096);

    REQUIRE(computeSlideId(twoScenes(), 4097) != base);

    auto widthChanged = twoScenes();
    widthChanged[0].width += 1;
    REQUIRE(computeSlideId(widthChanged, 4096) != base);

    auto heightChanged = twoScenes();
    heightChanged[0].height += 1;
    REQUIRE(computeSlideId(heightChanged, 4096) != base);

    auto channelsChanged = twoScenes();
    channelsChanged[0].numChannels += 1;
    REQUIRE(computeSlideId(channelsChanged, 4096) != base);

    // A field of a scene other than the first must count too.
    auto secondSceneChanged = twoScenes();
    secondSceneChanged[1].width += 1;
    REQUIRE(computeSlideId(secondSceneChanged, 4096) != base);

    auto indexChanged = twoScenes();
    indexChanged[1].index = 7;
    REQUIRE(computeSlideId(indexChanged, 4096) != base);

    auto sceneDropped = twoScenes();
    sceneDropped.pop_back();
    REQUIRE(computeSlideId(sceneDropped, 4096) != base);
}

TEST_CASE("computeSlideId separates adjacent numeric fields", "[core][SlideId]")
{
    // Without a separator, (width 1, height 00) and (width 10, height 0)
    // flatten to the same digits.
    const std::vector<SceneInfo> a{makeScene(0, 1, 100, 3)};
    const std::vector<SceneInfo> b{makeScene(0, 11, 0, 3)};

    REQUIRE(computeSlideId(a, 4096) != computeSlideId(b, 4096));
}

TEST_CASE("computeSlideId is identical for every scene of one file", "[core][SlideId]")
{
    // Build the SlideInfo as each scene in turn would produce it: the
    // open-scene fields differ, the scene table does not. One ID must result.
    SlideInfo asSceneZero;
    asSceneZero.scenes = twoScenes();
    asSceneZero.width = 100000;
    asSceneZero.height = 80000;
    asSceneZero.numChannels = 3;
    asSceneZero.numZoomLevels = 9;
    asSceneZero.resolutionX = 0.00000025;
    asSceneZero.driverName = "SVS";
    asSceneZero.driverId = "SVS";

    SlideInfo asSceneOne = asSceneZero;
    asSceneOne.width = 2048;
    asSceneOne.height = 1024;
    asSceneOne.numZoomLevels = 2;
    asSceneOne.resolutionX = 0.0000005;
    asSceneOne.driverId = ""; // auto-detected open of the same file

    REQUIRE(computeSlideId(asSceneZero, 4096) == computeSlideId(asSceneOne, 4096));
}

TEST_CASE("computeSlideId overloads agree", "[core][SlideId]")
{
    SlideInfo info;
    info.scenes = twoScenes();
    info.width = 100000;

    REQUIRE(computeSlideId(info, 4096) == computeSlideId(info.scenes, 4096));
}

TEST_CASE("computeSlideId tolerates an empty scene vector", "[core][SlideId]")
{
    // enumerateScenes returns empty for a file it cannot read. The hash stays
    // total; refusing the lookup is the resolver's job, not this function's.
    const std::string id = computeSlideId({}, 0);

    REQUIRE(id.size() == 16);
    REQUIRE(id == computeSlideId({}, 0));
    REQUIRE(id != computeSlideId(twoScenes(), 4096));
}

TEST_CASE("computeSlideId does not collide across realistic slides", "[core][SlideId]")
{
    // Review Focus 4: pins the collision surface so it cannot silently widen.
    // Review Focus 3: a slide re-saved to a different size is a different ID,
    // which is a clean miss rather than a wrong match.
    const std::vector<std::pair<std::vector<SceneInfo>, uint64_t>> corpus{
        {{makeScene(0, 100000, 80000, 3)}, 2'400'000'000ULL},
        {{makeScene(0, 100000, 80000, 3)}, 2'400'000'001ULL}, // re-saved
        {{makeScene(0, 100000, 80000, 4)}, 2'400'000'000ULL},
        {{makeScene(0, 80000, 100000, 3)}, 2'400'000'000ULL},
        {{makeScene(0, 46000, 32914, 3)}, 1'073'741'824ULL},
        {{makeScene(0, 1024, 1024, 1), makeScene(1, 512, 512, 1)}, 10'485'760ULL},
        {{makeScene(0, 1024, 1024, 1)}, 10'485'760ULL},
        {{makeScene(0, 512, 512, 1), makeScene(1, 1024, 1024, 1)}, 10'485'760ULL},
        {{}, 0ULL},
    };

    std::set<std::string> ids;
    for (const auto& entry : corpus) {
        ids.insert(computeSlideId(entry.first, entry.second));
    }

    REQUIRE(ids.size() == corpus.size());
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

In `tests/CMakeLists.txt`, add `core/SlideIdTest.cpp` to the `slideio-viewer-core-tests` source list, after `core/ColorManagementTest.cpp`.

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: FAIL at compile time — `slideio/viewer/core/SlideId.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/core/include/slideio/viewer/core/SlideId.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// A 16-character lowercase hex identity derived from a slide's content
/// properties: the file size and the geometry of every scene it holds.
///
/// Stable across moves and renames, and identical whichever scene of a
/// multi-scene file is open -- every input is file-level. The open scene's own
/// dimensions, channel count, pyramid depth and resolution are deliberately
/// excluded, since each varies by scene and would key one file to several IDs.
/// `driverId` is excluded too: it is empty until the slide has been opened,
/// which is after the point the identity is needed, and a file opened by
/// auto-detection is the same slide as one opened with its driver named.
///
/// NOT cryptographic and NOT tamper-evident. The hash is FNV-1a, chosen because
/// 64 bits is all a 16-hex identity can carry and a vendored crypto
/// implementation would buy a property nothing here uses. This identifies a
/// slide for the user's own stored settings; nothing authenticates against it.
/// A consumer must never treat a matching ID as proof of provenance.
///
/// Total: an empty scene vector yields a stable ID rather than throwing.
/// Callers that cannot enumerate a slide must decline to look anything up
/// rather than trust the ID that results -- every unreadable file produces it.
std::string computeSlideId(const std::vector<SceneInfo>& scenes, uint64_t fileSizeBytes);

/// Convenience overload over `info.scenes`. Delegates to the primitive above so
/// an identity computed before a slide is opened and one computed afterwards
/// agree by construction, not by two call sites staying in step.
std::string computeSlideId(const SlideInfo& info, uint64_t fileSizeBytes);

} // namespace slideio::viewer::core
```

- [ ] **Step 4: Write the implementation**

Create `src/core/src/SlideId.cpp`:

```cpp
#include "slideio/viewer/core/SlideId.h"

#include <cstdio>
#include <string>

namespace slideio::viewer::core
{

namespace
{

constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

// ASCII unit separator. Cannot occur in any input, all of which are decimal
// digits, so adjacent fields can never run together into a different reading.
constexpr char kFieldSeparator = '\x1F';

void appendField(std::string& buffer, uint64_t value)
{
    buffer += std::to_string(value);
    buffer += kFieldSeparator;
}

void appendField(std::string& buffer, int value)
{
    buffer += std::to_string(value);
    buffer += kFieldSeparator;
}

uint64_t fnv1a64(const std::string& bytes)
{
    uint64_t hash = kFnvOffsetBasis;
    for (const char c : bytes) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

std::string computeSlideId(const std::vector<SceneInfo>& scenes, uint64_t fileSizeBytes)
{
    std::string input;
    input.reserve(32 + scenes.size() * 32);

    appendField(input, fileSizeBytes);
    appendField(input, static_cast<uint64_t>(scenes.size()));

    for (const SceneInfo& scene : scenes) {
        appendField(input, scene.index);
        appendField(input, scene.width);
        appendField(input, scene.height);
        appendField(input, scene.numChannels);
    }

    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(fnv1a64(input)));
    return std::string(hex);
}

std::string computeSlideId(const SlideInfo& info, uint64_t fileSizeBytes)
{
    return computeSlideId(info.scenes, fileSizeBytes);
}

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Register the source and run the tests**

In `src/core/CMakeLists.txt`, add `src/SlideId.cpp` to the `slideio-viewer-core` source list, after `src/ColorManagement.cpp`.

Run:
```bash
./build.sh && ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```
Expected: PASS, all eight `[SlideId]` cases.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/SlideId.h src/core/src/SlideId.cpp \
        src/core/CMakeLists.txt tests/core/SlideIdTest.cpp tests/CMakeLists.txt
git commit -m "Derive a content-based identity for a slide"
```

---

### Task 2: `core` — override record, store interface, and the types the adapter takes

**Files:**
- Create: `src/core/include/slideio/viewer/core/ColorProfileOverride.h`
- Modify: `src/core/include/slideio/viewer/core/Types.h` (add `ColorProfileOrigin`; add two `SlideInfo` fields)
- Test: `tests/core/ColorProfileOverrideTest.cpp`
- Modify: `tests/CMakeLists.txt`

No `.cpp` — this task is types and one pure resolution helper that is header-inline free of state. The helper has a `.cpp` only if it grows; it does not here.

**Interfaces:**
- Consumes: `computeSlideId` from Task 1.
- Produces: `ColorProfileOverride`, `IColorProfileOverrideStore`, `SuppliedColorProfile`, `ColorProfileOrigin`, and the `SlideInfo::colorProfileOrigin` / `SlideInfo::displacedEmbeddedProfile` fields. Task 3 implements the interface; Task 4 takes `SuppliedColorProfile`; Tasks 5-8 read the rest.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ColorProfileOverrideTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/ColorProfileOverride.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace slideio::viewer::core;

namespace
{

// A store with no QSettings behind it, so core keeps its own tests Qt-free.
class FakeOverrideStore : public IColorProfileOverrideStore
{
public:
    std::optional<ColorProfileOverride> find(const std::string& slideId) const override
    {
        const auto it = m_entries.find(slideId);
        if (it == m_entries.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void set(const ColorProfileOverride& entry) override
    {
        m_entries[entry.slideId] = entry;
    }

    void remove(const std::string& slideId) override
    {
        m_entries.erase(slideId);
    }

    std::vector<ColorProfileOverride> all() const override
    {
        std::vector<ColorProfileOverride> out;
        out.reserve(m_entries.size());
        for (const auto& pair : m_entries) {
            out.push_back(pair.second);
        }
        return out;
    }

private:
    std::unordered_map<std::string, ColorProfileOverride> m_entries;
};

ColorProfileOverride makeEntry(const std::string& slideId, const std::string& profilePath)
{
    ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = profilePath;
    entry.slideDisplayName = "case001_HE.svs";
    entry.displacedEmbedded = true;
    return entry;
}

} // namespace

TEST_CASE("store round-trips an entry", "[core][ColorProfileOverride]")
{
    FakeOverrideStore store;
    store.set(makeEntry("a3f8c2e109b74d21", "C:/profiles/x.icc"));

    const auto found = store.find("a3f8c2e109b74d21");
    REQUIRE(found.has_value());
    REQUIRE(found->profilePath == "C:/profiles/x.icc");
    REQUIRE(found->slideDisplayName == "case001_HE.svs");
    REQUIRE(found->displacedEmbedded);
}

TEST_CASE("store reports a miss as empty, not as a default entry",
          "[core][ColorProfileOverride]")
{
    FakeOverrideStore store;
    REQUIRE_FALSE(store.find("0000000000000000").has_value());
}

TEST_CASE("store removes one entry and leaves the rest",
          "[core][ColorProfileOverride]")
{
    FakeOverrideStore store;
    store.set(makeEntry("aaaaaaaaaaaaaaaa", "C:/profiles/a.icc"));
    store.set(makeEntry("bbbbbbbbbbbbbbbb", "C:/profiles/b.icc"));

    store.remove("aaaaaaaaaaaaaaaa");

    REQUIRE_FALSE(store.find("aaaaaaaaaaaaaaaa").has_value());
    REQUIRE(store.find("bbbbbbbbbbbbbbbb").has_value());
    REQUIRE(store.all().size() == 1);
}

TEST_CASE("a supplied profile defaults to not displacing",
          "[core][ColorProfileOverride]")
{
    // The default matters: it is what the global default profile relies on to
    // keep leaving an embedded profile alone.
    SuppliedColorProfile supplied;
    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes.empty());
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add `core/ColorProfileOverrideTest.cpp` to `slideio-viewer-core-tests` in `tests/CMakeLists.txt`.

Run: `./build.sh`
Expected: FAIL at compile time — `ColorProfileOverride.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/core/include/slideio/viewer/core/ColorProfileOverride.h`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// One user decision: this slide is to be displayed through this ICC profile.
struct ColorProfileOverride
{
    std::string slideId;            ///< computeSlideId result
    std::string profilePath;        ///< absolute path to the .icc/.icm file
    std::string slideDisplayName;   ///< last-known filename, for the dialog only
    bool displacedEmbedded = false; ///< set over a slide that embeds a profile
};

/// Persistence of the user's per-slide profile choices.
///
/// `slideDisplayName` is carried for display alone and is never matched
/// against, so a renamed slide shows a stale name in the manage dialog rather
/// than losing its override.
class IColorProfileOverrideStore
{
public:
    virtual ~IColorProfileOverrideStore() = default;

    virtual std::optional<ColorProfileOverride> find(const std::string& slideId) const = 0;
    virtual void set(const ColorProfileOverride& entry) = 0;
    virtual void remove(const std::string& slideId) = 0;
    virtual std::vector<ColorProfileOverride> all() const = 0;
};

/// The profile bytes handed to an adapter, and the authority they carry.
///
/// The distinction is the whole feature: a global default stands in only for a
/// slide that embeds nothing, while a per-slide override displaces whatever the
/// slide embeds. `isSlideOverride` is what buildManagedScene consults to tell
/// the two apart.
struct SuppliedColorProfile
{
    std::vector<uint8_t> bytes;
    bool isSlideOverride = false;
};

} // namespace slideio::viewer::core
```

- [ ] **Step 4: Add the origin enum and the SlideInfo fields**

In `src/core/include/slideio/viewer/core/Types.h`, directly after the `ColorProfileSource` enum (which ends at the line `};` following `Supplied,`), add:

```cpp
/// Where the profile actually in use came from, as the viewer sees it.
///
/// Distinct from ColorProfileSource, which mirrors slideio::ColorProfileSource
/// and reports both a global default and a per-slide override as `Supplied` --
/// the library has no reason to tell them apart, and the panel must.
enum class ColorProfileOrigin
{
    Library,        ///< the slide's own profile, or SlideIO's assumption
    DefaultSetting, ///< the global default profile stood in
    SlideOverride,  ///< a profile the user chose for this slide
};
```

In the same file, inside `struct SlideInfo`, directly after the line
`std::string colorManagementDetail;  // SlideIO's message when colorManagement == BindFailed`, add:

```cpp
    ColorProfileOrigin colorProfileOrigin = ColorProfileOrigin::Library;
    // True when a per-slide override is in use on a slide that embeds its own
    // profile. The panel says so, so a slide shown through a non-native profile
    // never looks native.
    bool displacedEmbeddedProfile = false;
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `./build.sh && ctest --test-dir build/build -C Release -R core-tests --output-on-failure`
Expected: PASS, all four `[ColorProfileOverride]` cases, and the `[SlideId]` cases still pass.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/ColorProfileOverride.h \
        src/core/include/slideio/viewer/core/Types.h \
        tests/core/ColorProfileOverrideTest.cpp tests/CMakeLists.txt
git commit -m "Declare the per-slide colour profile override and its store"
```

---

### Task 3: `infra` — the QSettings-backed store

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/QSettingsColorProfileOverrideStore.h`
- Create: `src/infra/src/QSettingsColorProfileOverrideStore.cpp`
- Test: `tests/infra/QSettingsColorProfileOverrideStoreTest.cpp`
- Modify: `src/infra/CMakeLists.txt` (source list; **add `Qt6::Core`**)
- Modify: `tests/CMakeLists.txt` (test source; **add the Qt bin dir to the `infra-tests` PATH**)

**Interfaces:**
- Consumes: `IColorProfileOverrideStore`, `ColorProfileOverride` from Task 2.
- Produces: `infra::QSettingsColorProfileOverrideStore`, default-constructed for the application and `explicit QSettingsColorProfileOverrideStore(const QString& iniFilePath)` for tests. Task 6 constructs the default form.

`slideio-viewer-infra` currently links `slideio-viewer-core`, `SlideIO::*` and `spdlog` only. It does **not** link Qt, despite the target table in `CLAUDE.md`. This task adds `Qt6::Core`, and `infra-tests` needs the Qt bin directory on PATH on Windows or the binary will not start.

- [ ] **Step 1: Write the failing test**

Create `tests/infra/QSettingsColorProfileOverrideStoreTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/QSettingsColorProfileOverrideStore.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

using namespace slideio::viewer::core;
using slideio::viewer::infra::QSettingsColorProfileOverrideStore;

namespace
{

ColorProfileOverride makeEntry(const std::string& slideId)
{
    ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = "C:/profiles/scanner.icc";
    entry.slideDisplayName = "case001_HE.svs";
    entry.displacedEmbedded = true;
    return entry;
}

} // namespace

TEST_CASE("QSettings store round-trips every field", "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("overrides.ini");

    {
        QSettingsColorProfileOverrideStore store(ini);
        store.set(makeEntry("a3f8c2e109b74d21"));
    }

    // A second store over the same file: this is the across-sessions claim.
    QSettingsColorProfileOverrideStore reopened(ini);
    const auto found = reopened.find("a3f8c2e109b74d21");

    REQUIRE(found.has_value());
    REQUIRE(found->slideId == "a3f8c2e109b74d21");
    REQUIRE(found->profilePath == "C:/profiles/scanner.icc");
    REQUIRE(found->slideDisplayName == "case001_HE.svs");
    REQUIRE(found->displacedEmbedded);
}

TEST_CASE("QSettings store reports a miss as empty", "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("overrides.ini"));

    REQUIRE_FALSE(store.find("0000000000000000").has_value());
}

TEST_CASE("QSettings store removes one entry and lists the rest",
          "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("overrides.ini"));

    store.set(makeEntry("aaaaaaaaaaaaaaaa"));
    store.set(makeEntry("bbbbbbbbbbbbbbbb"));
    store.remove("aaaaaaaaaaaaaaaa");

    REQUIRE_FALSE(store.find("aaaaaaaaaaaaaaaa").has_value());
    REQUIRE(store.all().size() == 1);
    REQUIRE(store.all().front().slideId == "bbbbbbbbbbbbbbbb");
}

TEST_CASE("QSettings store overwrites rather than duplicating",
          "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("overrides.ini"));

    store.set(makeEntry("a3f8c2e109b74d21"));
    ColorProfileOverride second = makeEntry("a3f8c2e109b74d21");
    second.profilePath = "C:/profiles/other.icc";
    second.displacedEmbedded = false;
    store.set(second);

    REQUIRE(store.all().size() == 1);
    REQUIRE(store.find("a3f8c2e109b74d21")->profilePath == "C:/profiles/other.icc");
    REQUIRE_FALSE(store.find("a3f8c2e109b74d21")->displacedEmbedded);
}

TEST_CASE("QSettings store starts empty on a fresh file", "[infra][OverrideStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettingsColorProfileOverrideStore store(dir.filePath("nothing-here.ini"));

    REQUIRE(store.all().empty());
}
```

- [ ] **Step 2: Wire the build and run the test to verify it fails**

In `src/infra/CMakeLists.txt`, add `src/QSettingsColorProfileOverrideStore.cpp` to the source list, and change the link line to:

```cmake
target_link_libraries(slideio-viewer-infra
    PUBLIC slideio-viewer-core
    PRIVATE SlideIO::slideio SlideIO::core SlideIO::transformer spdlog::spdlog Qt6::Core
)
```

In `tests/CMakeLists.txt`, add `infra/QSettingsColorProfileOverrideStoreTest.cpp` to `slideio-viewer-infra-tests`, and change the `infra-tests` environment line so Qt is found as well as SlideIO:

```cmake
        set_tests_properties(infra-tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_slideio_bin_dir};PATH=path_list_prepend:${_qt_bin_dir}")
```

Run: `./build.sh`
Expected: FAIL at compile time — `QSettingsColorProfileOverrideStore.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/infra/include/slideio/viewer/infra/QSettingsColorProfileOverrideStore.h`:

```cpp
#pragma once

#include "slideio/viewer/core/ColorProfileOverride.h"

#include <QString>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QSettings;

namespace slideio::viewer::infra
{

/// Per-slide profile overrides, persisted in QSettings under
/// "color/slideProfiles/<slideId>".
///
/// Nothing is pruned. Each entry is a deliberate choice the user made about one
/// slide and is a few hundred bytes; evicting one silently would discard that
/// choice without telling them. Removal is explicit.
class QSettingsColorProfileOverrideStore : public core::IColorProfileOverrideStore
{
public:
    /// The application's own settings, as QSettings resolves them from the
    /// organisation and application names.
    QSettingsColorProfileOverrideStore();

    /// An explicit INI file. Tests use this so they never touch the user's
    /// real configuration.
    explicit QSettingsColorProfileOverrideStore(const QString& iniFilePath);

    ~QSettingsColorProfileOverrideStore() override;

    std::optional<core::ColorProfileOverride> find(const std::string& slideId) const override;
    void set(const core::ColorProfileOverride& entry) override;
    void remove(const std::string& slideId) override;
    std::vector<core::ColorProfileOverride> all() const override;

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace slideio::viewer::infra
```

- [ ] **Step 4: Write the implementation**

Create `src/infra/src/QSettingsColorProfileOverrideStore.cpp`:

```cpp
#include "slideio/viewer/infra/QSettingsColorProfileOverrideStore.h"

#include <QSettings>
#include <QStringList>

namespace slideio::viewer::infra
{

namespace
{

constexpr auto kGroup = "color/slideProfiles";
constexpr auto kProfilePathKey = "profilePath";
constexpr auto kDisplayNameKey = "slideDisplayName";
constexpr auto kDisplacedKey = "displacedEmbedded";

QString entryKey(const std::string& slideId, const char* field)
{
    return QString("%1/%2/%3")
        .arg(kGroup, QString::fromStdString(slideId), QString::fromLatin1(field));
}

} // namespace

QSettingsColorProfileOverrideStore::QSettingsColorProfileOverrideStore()
    : m_settings(std::make_unique<QSettings>())
{
}

QSettingsColorProfileOverrideStore::QSettingsColorProfileOverrideStore(const QString& iniFilePath)
    : m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

QSettingsColorProfileOverrideStore::~QSettingsColorProfileOverrideStore() = default;

std::optional<core::ColorProfileOverride> QSettingsColorProfileOverrideStore::find(
    const std::string& slideId) const
{
    const QString path = m_settings->value(entryKey(slideId, kProfilePathKey)).toString();
    if (path.isEmpty()) {
        return std::nullopt;
    }

    core::ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = path.toStdString();
    entry.slideDisplayName =
        m_settings->value(entryKey(slideId, kDisplayNameKey)).toString().toStdString();
    entry.displacedEmbedded =
        m_settings->value(entryKey(slideId, kDisplacedKey), false).toBool();
    return entry;
}

void QSettingsColorProfileOverrideStore::set(const core::ColorProfileOverride& entry)
{
    m_settings->setValue(entryKey(entry.slideId, kProfilePathKey),
                         QString::fromStdString(entry.profilePath));
    m_settings->setValue(entryKey(entry.slideId, kDisplayNameKey),
                         QString::fromStdString(entry.slideDisplayName));
    m_settings->setValue(entryKey(entry.slideId, kDisplacedKey), entry.displacedEmbedded);
    m_settings->sync();
}

void QSettingsColorProfileOverrideStore::remove(const std::string& slideId)
{
    m_settings->remove(QString("%1/%2").arg(kGroup, QString::fromStdString(slideId)));
    m_settings->sync();
}

std::vector<core::ColorProfileOverride> QSettingsColorProfileOverrideStore::all() const
{
    std::vector<core::ColorProfileOverride> out;

    m_settings->beginGroup(kGroup);
    const QStringList ids = m_settings->childGroups();
    m_settings->endGroup();

    out.reserve(static_cast<size_t>(ids.size()));
    for (const QString& id : ids) {
        if (auto entry = find(id.toStdString())) {
            out.push_back(*entry);
        }
    }
    return out;
}

} // namespace slideio::viewer::infra
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `./build.sh && ctest --test-dir build/build -C Release -R infra-tests --output-on-failure`
Expected: PASS, all five `[OverrideStore]` cases.

- [ ] **Step 6: Commit**

```bash
git add src/infra/include/slideio/viewer/infra/QSettingsColorProfileOverrideStore.h \
        src/infra/src/QSettingsColorProfileOverrideStore.cpp \
        src/infra/CMakeLists.txt \
        tests/infra/QSettingsColorProfileOverrideStoreTest.cpp tests/CMakeLists.txt
git commit -m "Persist per-slide colour profile overrides in QSettings"
```

---

### Task 4: `infra` — let an override displace an embedded profile

**Files:**
- Modify: `src/infra/include/slideio/viewer/infra/SlideIOAdapter.h:38-46` (both constructors)
- Modify: `src/infra/src/SlideIOAdapter.cpp:313-357` (`buildManagedScene`), `:560`, `:641`, `:774`, `:843` (constructor signatures and call sites)
- Test: `tests/infra/ColorManagedAdapterTest.cpp` (extend)

**Interfaces:**
- Consumes: `core::SuppliedColorProfile` from Task 2.
- Produces: both `SlideIOAdapter` constructors now take `core::SuppliedColorProfile supplied = {}` where they took `std::vector<uint8_t> defaultProfileBytes = {}`. Task 5 passes it.

- [ ] **Step 1: Read the existing test to match its skip and corpus conventions**

Run: `sed -n '1,60p' tests/infra/ColorManagedAdapterTest.cpp`

This test already resolves the corpus from `SLIDEIO_VIEWER_TEST_IMAGES` and skips when it is unset. Reuse that helper exactly; do not invent a second one. The new case must skip the same way, or CI without the corpus fails.

- [ ] **Step 2: Write the failing test**

Append to `tests/infra/ColorManagedAdapterTest.cpp`. Use the file's own `imagePath` /
`haveImage` / `SKIP` convention exactly as the cases above it do — do not invent a
second corpus helper, and do not read a standalone `.icc` from the corpus, which has
none. The override bytes come from a *different* slide's embedded profile, via the
existing `SlideIOAdapter::embeddedProfileBytes()`.

```cpp
TEST_CASE("a per-slide override displaces an embedded profile",
          "[infra][ColorManagement][override]")
{
    // JP2K-33003-1.svs embeds a profile -- the cases above rely on that.
    const std::string path = imagePath("svs/JP2K-33003-1.svs");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    const std::string donorPath = imagePath("czi/08_18_2018_enc_1001_633.czi");
    if (!haveImage(donorPath)) { SKIP("test image corpus not available"); }

    // A profile that is valid but is not the one this slide embeds.
    SlideIOAdapter donor(donorPath);
    const std::vector<uint8_t> foreignProfile = donor.embeddedProfileBytes();
    if (foreignProfile.empty()) { SKIP("donor slide embeds no profile"); }

    // Same bytes, different authority. A default must never displace an
    // embedded profile; an override must. Pinning both directions means the
    // new condition cannot loosen the default's rule by accident.
    core::SuppliedColorProfile asDefault;
    asDefault.bytes = foreignProfile;
    asDefault.isSlideOverride = false;

    core::SuppliedColorProfile asOverride;
    asOverride.bytes = foreignProfile;
    asOverride.isSlideOverride = true;

    SlideIOAdapter withDefault(path, 0, "", asDefault);
    SlideIOAdapter withOverride(path, 0, "", asOverride);

    withDefault.setColorMode(core::ColorMode::Managed);
    withOverride.setColorMode(core::ColorMode::Managed);

    const core::TileKey key(2, 0, 0, 0, 0, core::ColorMode::Managed);
    const core::TileData viaEmbedded = withDefault.readTile(key);
    const core::TileData viaOverride = withOverride.readTile(key);

    REQUIRE_FALSE(viaEmbedded.pixels.empty());
    REQUIRE(viaEmbedded.pixels.size() == viaOverride.pixels.size());

    // The slide embeds a profile, so the default was ignored and the override
    // was not. If these are equal, the override did not displace.
    REQUIRE(viaEmbedded.pixels != viaOverride.pixels);
}

TEST_CASE("an override sets the origin on SlideInfo",
          "[infra][ColorManagement][override]")
{
    const std::string path = imagePath("svs/JP2K-33003-1.svs");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    const std::string donorPath = imagePath("czi/08_18_2018_enc_1001_633.czi");
    if (!haveImage(donorPath)) { SKIP("test image corpus not available"); }

    SlideIOAdapter donor(donorPath);
    const std::vector<uint8_t> foreignProfile = donor.embeddedProfileBytes();
    if (foreignProfile.empty()) { SKIP("donor slide embeds no profile"); }

    core::SuppliedColorProfile asOverride;
    asOverride.bytes = foreignProfile;
    asOverride.isSlideOverride = true;

    SlideIOAdapter withOverride(path, 0, "", asOverride);
    const core::SlideInfo info = withOverride.slideInfo();

    REQUIRE(info.colorProfileOrigin == core::ColorProfileOrigin::SlideOverride);
    REQUIRE(info.displacedEmbeddedProfile);

    // A default over the same slide is not used at all, so the origin stays
    // with the slide's own profile and nothing is displaced.
    core::SuppliedColorProfile asDefault;
    asDefault.bytes = foreignProfile;
    asDefault.isSlideOverride = false;

    SlideIOAdapter withDefault(path, 0, "", asDefault);
    const core::SlideInfo defaulted = withDefault.slideInfo();

    REQUIRE(defaulted.colorProfileOrigin == core::ColorProfileOrigin::Library);
    REQUIRE_FALSE(defaulted.displacedEmbeddedProfile);
}
```

If the donor slide turns out to embed no profile, pick any other corpus slide the
existing cases prove has one; the assertions do not depend on which.

- [ ] **Step 3: Run the test to verify it fails**

Run: `./build.sh && ctest --test-dir build/build -C Release -R infra-tests --output-on-failure`
Expected: FAIL at compile time — no `SlideIOAdapter` constructor takes a `SuppliedColorProfile`.

- [ ] **Step 4: Change the constructor signatures**

In `src/infra/include/slideio/viewer/infra/SlideIOAdapter.h`, add `#include "slideio/viewer/core/ColorProfileOverride.h"` to the project includes, then replace the two constructor declarations:

```cpp
    // `supplied` carries the profile to use when the slide's own is absent or
    // is being overridden, and says which of those two it is: a default stands
    // in only for a slide that embeds nothing, while a per-slide override
    // displaces whatever the slide embeds. Empty bytes mean neither is set.
    explicit SlideIOAdapter(const std::string& filePath, int sceneIndex = 0,
                             const std::string& driverId = "",
                             core::SuppliedColorProfile supplied = {});
    SlideIOAdapter(const std::string& filePath, const std::string& auxImageName,
                   const std::string& driverId = "",
                   core::SuppliedColorProfile supplied = {});
```

- [ ] **Step 5: Change the condition in `buildManagedScene`**

In `src/infra/src/SlideIOAdapter.cpp`, change the `buildManagedScene` parameter from
`const std::vector<uint8_t>& defaultProfileBytes` to `const core::SuppliedColorProfile& supplied`,
replace `const bool haveDefault = !defaultProfileBytes.empty();` with
`const bool haveBytes = !supplied.bytes.empty();`, update the `NoProfile` gate to use
`haveBytes`, and replace the override block with:

```cpp
        // A default stands in only for a slide that embeds nothing: displacing
        // an embedded profile is not what a default means. A per-slide
        // override is the opposite case -- the user is saying this slide's own
        // profile is the wrong one -- so it displaces, which is what
        // setSourceProfileOverride has always been able to do.
        if (haveBytes && (supplied.isSlideOverride || !info.colorProfileInfo.present)) {
            cm.setSourceProfileOverride(::slideio::ColorProfile(supplied.bytes));
        }
```

- [ ] **Step 6: Update the two constructors and their call sites**

At `src/infra/src/SlideIOAdapter.cpp:560` and `:774`, change the trailing parameter from
`std::vector<uint8_t> defaultProfileBytes` to `core::SuppliedColorProfile supplied`.
At `:641` and `:843`, change the `buildManagedScene(...)` argument from
`defaultProfileBytes` to `supplied`.

Set the origin while you are in there, so Task 7 has something to display. After each
`m_slideInfo.colorManagement = buildManagedScene(...)` line, add:

```cpp
    // Mirrors the condition inside buildManagedScene exactly. Supplying bytes
    // is not the same as using them: a default handed to a slide that embeds
    // its own profile is ignored, and reporting that slide as shown through
    // the default setting would be a lie the panel then tells the user.
    const bool suppliedWasUsed =
        !supplied.bytes.empty()
        && (supplied.isSlideOverride || !m_slideInfo.colorProfileInfo.present)
        && m_slideInfo.colorManagement == core::ColorManagementAvailability::Available;

    if (suppliedWasUsed) {
        m_slideInfo.colorProfileOrigin = supplied.isSlideOverride
            ? core::ColorProfileOrigin::SlideOverride
            : core::ColorProfileOrigin::DefaultSetting;
        m_slideInfo.displacedEmbeddedProfile =
            supplied.isSlideOverride && m_slideInfo.colorProfileInfo.present;
    }
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `./build.sh && ctest --test-dir build/build -C Release --output-on-failure`
Expected: PASS. The whole suite, not just infra — the constructor signature change reaches `ViewportWidget`, which must still compile. If `ViewportWidget.cpp:1156` and `:1209` fail to compile, that is expected: fix them by wrapping the bytes in a `SuppliedColorProfile{std::move(defaultProfileBytes), false}` as a temporary measure. Task 5 replaces that properly.

- [ ] **Step 8: Commit**

```bash
git add src/infra/include/slideio/viewer/infra/SlideIOAdapter.h \
        src/infra/src/SlideIOAdapter.cpp src/ui/src/ViewportWidget.cpp \
        tests/infra/ColorManagedAdapterTest.cpp
git commit -m "Let a per-slide profile displace the one a slide embeds"
```

---

### Task 5: `ui` — resolve the profile at open

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h:74` (replace `setDefaultColorProfile`), and add `reopenCurrentScene`
- Modify: `src/ui/src/ViewportWidget.cpp` — `SceneOpenResult` (`:44-61`), `Impl` state (`:1296-1311`), `openSceneSync` (`:1153`, `:1209`), `openSlide` (`:1839-1869`), `openScene` (`:2063`), `openAuxImage` (`:2123`), `setDefaultColorProfile` (`:2347`)
- Create: `src/ui/include/slideio/viewer/ui/ColorProfilePolicy.h`
- Create: `src/ui/src/ColorProfilePolicy.cpp`
- Test: `tests/ui/ColorProfilePolicyTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `computeSlideId` (Task 1), `SuppliedColorProfile` (Task 2), `inspectIccHeader` / `classifyDefaultProfile` / `defaultProfileProblemText` (existing `core/ColorManagement.h`).
- Produces: `ui::ColorProfilePolicy`, `ui::resolveColorProfile(const ColorProfilePolicy&, const std::vector<core::SceneInfo>&, uint64_t, std::string& outProblem)`, `ViewportWidget::setColorProfilePolicy(ColorProfilePolicy)`, `ViewportWidget::reopenCurrentScene()`. Task 6 calls all three.

The resolution function is pure and lives apart from the widget precisely so it can be tested without a GL context or an open slide.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/ColorProfilePolicyTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/ColorProfilePolicy.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

core::SceneInfo makeScene(int index, int width, int height)
{
    core::SceneInfo s;
    s.index = index;
    s.width = width;
    s.height = height;
    s.numChannels = 3;
    return s;
}

// A minimal but structurally valid RGB ICC profile header: 128 header bytes
// with the size, 'RGB ' data space and 'acsp' signature in the right places,
// which is all inspectIccHeader reads.
std::vector<uint8_t> validRgbProfileBytes()
{
    std::vector<uint8_t> bytes(128, 0);
    const uint32_t size = 128;
    bytes[0] = static_cast<uint8_t>((size >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((size >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((size >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(size & 0xFF);
    bytes[16] = 'R'; bytes[17] = 'G'; bytes[18] = 'B'; bytes[19] = ' ';
    bytes[36] = 'a'; bytes[37] = 'c'; bytes[38] = 's'; bytes[39] = 'p';
    return bytes;
}

QString writeFile(const QString& path, const std::vector<uint8_t>& bytes)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
    f.close();
    return path;
}

} // namespace

TEST_CASE("no override falls back to the default", "[ui][ColorProfilePolicy]")
{
    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, {makeScene(0, 100, 100)}, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
    REQUIRE(problem.empty());
}

TEST_CASE("a matching override wins and is marked as one", "[ui][ColorProfilePolicy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString iccPath = writeFile(dir.filePath("slide.icc"), validRgbProfileBytes());

    const std::vector<core::SceneInfo> scenes{makeScene(0, 100, 100)};
    const std::string slideId = core::computeSlideId(scenes, 4096);

    ui::ColorProfilePolicy policy;
    policy.defaultBytes = {1, 2, 3};
    policy.overridePathsBySlideId[slideId] = iccPath.toStdString();

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, scenes, 4096, problem);

    REQUIRE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == validRgbProfileBytes());
    REQUIRE(problem.empty());
}

TEST_CASE("an override for a different slide does not apply",
          "[ui][ColorProfilePolicy]")
{
    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();
    policy.overridePathsBySlideId["ffffffffffffffff"] = "C:/profiles/other.icc";

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, {makeScene(0, 100, 100)}, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(problem.empty());
}

TEST_CASE("an empty scene list never matches an override",
          "[ui][ColorProfilePolicy]")
{
    // Review Focus 1. enumerateScenes returns empty for a file it cannot read.
    // Every such file hashes alike, so a lookup would apply one slide's
    // override to an unrelated one. The lookup must be declined outright.
    ui::ColorProfilePolicy policy;
    policy.defaultBytes = {9, 9, 9};
    policy.overridePathsBySlideId[core::computeSlideId({}, 0)] = "C:/profiles/x.icc";

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, {}, 0, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
}

TEST_CASE("an unreadable override falls back and reports why",
          "[ui][ColorProfilePolicy]")
{
    // Review Focus 2, in its reachable form: a path that cannot be read must
    // not fail the open, and must not blame the slide.
    const std::vector<core::SceneInfo> scenes{makeScene(0, 100, 100)};

    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();
    policy.overridePathsBySlideId[core::computeSlideId(scenes, 4096)] =
        "//unreachable-share/no/such/profile.icc";

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, scenes, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
    REQUIRE_FALSE(problem.empty());
    REQUIRE(problem.find("profile.icc") != std::string::npos);
}

TEST_CASE("an override that is not an RGB profile falls back",
          "[ui][ColorProfilePolicy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString junk = writeFile(dir.filePath("notaprofile.icc"),
                                   std::vector<uint8_t>(128, 0x7E));

    const std::vector<core::SceneInfo> scenes{makeScene(0, 100, 100)};

    ui::ColorProfilePolicy policy;
    policy.defaultBytes = validRgbProfileBytes();
    policy.overridePathsBySlideId[core::computeSlideId(scenes, 4096)] = junk.toStdString();

    std::string problem;
    const auto supplied = ui::resolveColorProfile(policy, scenes, 4096, problem);

    REQUIRE_FALSE(supplied.isSlideOverride);
    REQUIRE(supplied.bytes == policy.defaultBytes);
    REQUIRE_FALSE(problem.empty());
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add `ui/ColorProfilePolicyTest.cpp` to `slideio-viewer-ui-tests` in `tests/CMakeLists.txt`.

Run: `./build.sh`
Expected: FAIL at compile time — `slideio/viewer/ui/ColorProfilePolicy.h: No such file or directory`.

- [ ] **Step 3: Write the policy header**

Create `src/ui/include/slideio/viewer/ui/ColorProfilePolicy.h`:

```cpp
#pragma once

#include "slideio/viewer/core/ColorProfileOverride.h"
#include "slideio/viewer/core/SlideId.h"
#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace slideio::viewer::ui
{

/// Everything the open path needs to choose a colour profile, snapshotted on
/// the UI thread and captured by value into the background open threads.
///
/// It holds no QSettings handle and no pointer back to MainWindow on purpose:
/// resolution runs on the open thread, where neither may be touched.
///
/// Paths rather than bytes, so the snapshot stays small when a user has many
/// overrides, and so a profile edited on disk takes effect at the next slide
/// open rather than at the next restart.
struct ColorProfilePolicy
{
    std::vector<uint8_t> defaultBytes; ///< already validated; empty if none
    std::unordered_map<std::string, std::string> overridePathsBySlideId;
};

/// Choose the profile for a slide, given its scene table and file size.
///
/// Precedence: a per-slide override whose file validates now, else the default.
/// (A slide's embedded profile outranks the default but not an override; that
/// part of the precedence lives in buildManagedScene, which is the only place
/// that knows what the slide embeds.)
///
/// An empty `scenes` is never matched against: enumeration failed, every such
/// slide hashes alike, and matching would apply one slide's override to
/// another.
///
/// `outProblem` is set, and the default returned, when an override is
/// configured but unusable -- so the user is told their setting is at fault
/// rather than being shown a message blaming the slide.
core::SuppliedColorProfile resolveColorProfile(const ColorProfilePolicy& policy,
                                               const std::vector<core::SceneInfo>& scenes,
                                               uint64_t fileSizeBytes,
                                               std::string& outProblem);

} // namespace slideio::viewer::ui
```

- [ ] **Step 4: Write the policy implementation**

Create `src/ui/src/ColorProfilePolicy.cpp`:

```cpp
#include "slideio/viewer/ui/ColorProfilePolicy.h"

#include "slideio/viewer/core/ColorManagement.h"

#include <QDir>
#include <QFile>
#include <QString>

namespace slideio::viewer::ui
{

core::SuppliedColorProfile resolveColorProfile(const ColorProfilePolicy& policy,
                                               const std::vector<core::SceneInfo>& scenes,
                                               uint64_t fileSizeBytes,
                                               std::string& outProblem)
{
    outProblem.clear();

    core::SuppliedColorProfile fallback;
    fallback.bytes = policy.defaultBytes;
    fallback.isSlideOverride = false;

    // Enumeration failed. The identity of an unreadable file is the identity of
    // every unreadable file, so looking it up could hand this slide an override
    // belonging to a different one.
    if (scenes.empty()) {
        return fallback;
    }

    const std::string slideId = core::computeSlideId(scenes, fileSizeBytes);
    const auto it = policy.overridePathsBySlideId.find(slideId);
    if (it == policy.overridePathsBySlideId.end()) {
        return fallback;
    }

    const QString path = QString::fromStdString(it->second);
    QFile file(path);
    const bool readable = file.open(QIODevice::ReadOnly);

    std::vector<uint8_t> bytes;
    if (readable) {
        const QByteArray raw = file.readAll();
        bytes.assign(raw.begin(), raw.end());
    }

    // Revalidated on every open, not only when the user picked the file: the
    // setting stores a path, so the file can be truncated, replaced or removed
    // afterwards.
    const core::DefaultProfileStatus status =
        core::classifyDefaultProfile(readable, core::inspectIccHeader(bytes));
    if (status != core::DefaultProfileStatus::Ok) {
        outProblem = core::defaultProfileProblemText(
            status, QDir::toNativeSeparators(path).toStdString());
        return fallback;
    }

    core::SuppliedColorProfile supplied;
    supplied.bytes = std::move(bytes);
    supplied.isSlideOverride = true;
    return supplied;
}

} // namespace slideio::viewer::ui
```

Add `src/ColorProfilePolicy.cpp` to the `slideio-viewer-ui` source list in `src/ui/CMakeLists.txt`.

- [ ] **Step 5: Run the policy tests to verify they pass**

Run: `./build.sh && ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`
Expected: PASS, all six `[ColorProfilePolicy]` cases.

- [ ] **Step 6: Commit the pure part**

```bash
git add src/ui/include/slideio/viewer/ui/ColorProfilePolicy.h \
        src/ui/src/ColorProfilePolicy.cpp src/ui/CMakeLists.txt \
        tests/ui/ColorProfilePolicyTest.cpp tests/CMakeLists.txt
git commit -m "Choose a slide's colour profile from a policy snapshot"
```

- [ ] **Step 7: Carry the policy through the open path**

In `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, add `#include "slideio/viewer/ui/ColorProfilePolicy.h"` and replace the `setDefaultColorProfile` declaration (line 74 and its comment) with:

```cpp
    // The profile policy to apply to slides opened from now on: the global
    // default, plus the per-slide overrides by slide id. Snapshotted by
    // MainWindow on the UI thread; the open threads capture it by value.
    void setColorProfilePolicy(ColorProfilePolicy policy);

    // Why a configured override could not be used on the slide now showing.
    // Empty when none was configured or it was fine.
    const std::string& lastColorProfileProblem() const;

    // Reopen whatever is currently displayed, so a profile change takes effect
    // without the user reopening the file by hand. A no-op when nothing is
    // open. Goes through the ordinary open path, so the openOpId guard still
    // decides which of several in-flight opens wins.
    void reopenCurrentScene();
```

In `src/ui/src/ViewportWidget.cpp`:

1. In `SceneOpenResult`, after `QImage thumbnail;`, add:
   ```cpp
       // Why a configured override could not be used, if it could not. Empty
       // otherwise. Surfaced like defaultProfileProblem, so the user is never
       // told their slide is at fault for a problem in their own setting.
       std::string colorProfileProblem;
   ```
2. In `Impl`, replace `std::vector<uint8_t> defaultColorProfile;` and its comment with:
   ```cpp
       // Snapshotted by MainWindow on the UI thread, captured by value into the
       // background open threads below.
       ColorProfilePolicy colorProfilePolicy;

       // What is currently displayed, so reopenCurrentScene can rebuild it.
       int currentSceneIndex = 0;
       std::string currentAuxImageName; // empty unless an aux image is shown
   ```
3. Change both `openSceneSync` overloads to take `core::SuppliedColorProfile supplied` instead of `std::vector<uint8_t> defaultProfileBytes`, and pass it straight to the `SlideIOAdapter` constructor (this replaces the temporary wrapping from Task 4 Step 7).
4. In `openSlide`, set `m_impl->currentSceneIndex = 0;` and `m_impl->currentAuxImageName.clear();` beside the existing `currentFilePath` assignment, then restructure the thread body so enumeration comes first:

```cpp
    ColorProfilePolicy policy = m_impl->colorProfilePolicy;
    std::thread([this, opId, filePath, driverId, statusCallback, policy]() {
        // Enumerated before the scene is opened, not after: the slide's
        // identity is derived from this list, and the adapter needs the
        // profile that identity selects at construction time. The scene panel
        // still gets the same list it always did.
        std::vector<core::SceneInfo> scenes;
        std::vector<core::SceneInfo> auxImages;
        try {
            auto enumResult = infra::SlideIOAdapter::enumerateScenes(filePath, driverId);
            scenes = std::move(enumResult.first);
            auxImages = std::move(enumResult.second);
        } catch (const std::exception& ex) {
            spdlog::warn("openSlide: failed to enumerate scenes: {}", ex.what());
        }

        uint64_t fileSize = 0;
        try {
            fileSize = static_cast<uint64_t>(std::filesystem::file_size(filePath));
        } catch (const std::exception& ex) {
            spdlog::warn("openSlide: failed to size '{}': {}", filePath, ex.what());
        }

        std::string problem;
        const core::SuppliedColorProfile supplied =
            resolveColorProfile(policy, scenes, fileSize, problem);

        SceneOpenResult result = openSceneSync(filePath, 0, driverId, statusCallback, supplied);
        result.scenes = std::move(scenes);
        result.auxImages = std::move(auxImages);
        result.colorProfileProblem = std::move(problem);

        QMetaObject::invokeMethod(this,
            [this, opId, r = std::move(result)]() mutable {
                installSceneOpenResult(opId, std::move(r));
            }, Qt::QueuedConnection);
    }).detach();
```

Add `#include <filesystem>` to the file's std includes.

5. In `openScene`, set `m_impl->currentSceneIndex = sceneIndex;` and clear `currentAuxImageName`, and resolve before spawning — `m_impl->slideInfo.scenes` is already populated for this file:

```cpp
    std::string problem;
    uint64_t fileSize = 0;
    try {
        fileSize = static_cast<uint64_t>(std::filesystem::file_size(filePath));
    } catch (const std::exception& ex) {
        spdlog::warn("openScene: failed to size '{}': {}", filePath, ex.what());
    }
    const core::SuppliedColorProfile supplied = resolveColorProfile(
        m_impl->colorProfilePolicy, m_impl->slideInfo.scenes, fileSize, problem);
```

then capture `supplied` and `problem` into the thread exactly as `defaultProfileBytes` was captured, pass `supplied` to `openSceneSync`, and set `result.colorProfileProblem = problem;`.

6. In `openAuxImage`, do the same, and additionally set `m_impl->currentAuxImageName = auxImageName;`.

7. In `installSceneOpenResult`, store the carried problem so the UI can read it:
   add `std::string colorProfileProblem;` to `Impl` and assign
   `m_impl->colorProfileProblem = std::move(r.colorProfileProblem);` alongside the
   other fields it installs. A problem set on a stale `opId` is discarded with the
   rest of that result, which is correct: it describes an open that lost.

8. Replace `ViewportWidget::setDefaultColorProfile` with:

```cpp
void ViewportWidget::setColorProfilePolicy(ColorProfilePolicy policy)
{
    m_impl->colorProfilePolicy = std::move(policy);
}

// Why a configured override could not be used on the slide now showing, if it
// could not. Empty when none was configured or it was fine.
const std::string& ViewportWidget::lastColorProfileProblem() const
{
    return m_impl->colorProfileProblem;
}

void ViewportWidget::reopenCurrentScene()
{
    if (!m_impl->slideOpen || m_impl->currentFilePath.empty()) {
        return;
    }

    const std::string filePath = m_impl->currentFilePath;
    const std::string driverId = m_impl->currentDriverId;
    const std::string auxName = m_impl->currentAuxImageName;
    const int sceneIndex = m_impl->currentSceneIndex;

    if (!auxName.empty()) {
        openAuxImage(filePath, auxName, driverId);
    } else {
        openScene(filePath, sceneIndex, driverId);
    }
}
```

- [ ] **Step 8: Build and run the whole suite**

Run: `./build.sh && ctest --test-dir build/build -C Release --output-on-failure`
Expected: PASS. `MainWindow.cpp` will fail to compile at the `setDefaultColorProfile` call site (`MainWindow.cpp:957`); change that one line to build a `ColorProfilePolicy` with `defaultBytes` set and no overrides, and call `setColorProfilePolicy`. Task 6 fills in the override half.

- [ ] **Step 9: Verify a slide still opens, and that reopens do not race**

Run the installed viewer and open a slide from the DICOM fixtures. The point is the reordered enumeration: every format takes this path, so a regression here is not confined to slides with profiles.

Expected: the slide opens, the scene panel is populated as before, and the minimap renders.

Then, for Review Focus 5: read `reopenCurrentScene` back and confirm it calls
`openScene` / `openAuxImage` rather than reaching into the open machinery directly —
those two increment `openOpId`, which is what makes a later open discard an earlier
one's result. Confirm by hand that switching scenes rapidly on a multi-scene slide
while a previous open is still loading leaves the last-requested scene on screen, not
whichever finished last.

The reason this is checked by eye: a test would need two overlapping async opens
against a live GL widget. The guard it relies on is pre-existing and already proven;
what is new is only that this path goes through it.

- [ ] **Step 10: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp \
        src/ui/src/MainWindow.cpp
git commit -m "Resolve a slide's colour profile before its adapter is built"
```

---

### Task 6: `ui` — the menu actions

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/MainWindow.h:31-44` (new private slots and helpers)
- Modify: `src/ui/src/MainWindow.cpp` — `Impl` actions (`:100-108`), `createActions` (`:164-176`), `createMenus` (`:221-224`), `connectSignals` (`:303-310`), `applyDefaultColorProfile` (`:925-958`)

**Interfaces:**
- Consumes: `QSettingsColorProfileOverrideStore` (Task 3), `ColorProfilePolicy` / `resolveColorProfile` / `reopenCurrentScene` (Task 5), `computeSlideId` (Task 1).
- Produces: `MainWindow::onSetSlideColorProfile()`, `onClearSlideColorProfile()`, `applyColorProfilePolicy()`, `currentSlideId()`. Task 8 calls `applyColorProfilePolicy`.

- [ ] **Step 1: Add the store and the policy rebuild**

In `MainWindow.h`, after `void applyDefaultColorProfile();`, declare:

```cpp
    void onSetSlideColorProfile();
    void onClearSlideColorProfile();

    // Rebuilds the whole profile policy -- the validated default plus every
    // stored override -- and hands it to the viewport. Called at startup and
    // whenever either half changes.
    void applyColorProfilePolicy();

    // Identity of the slide on screen, or empty when none is open or its
    // scenes could not be enumerated.
    std::string currentSlideId() const;
```

In `MainWindow.cpp`'s `Impl`, after `std::string defaultProfileProblem;`, add:

```cpp
    QAction* setSlideProfileAction = nullptr;
    QAction* clearSlideProfileAction = nullptr;
    std::unique_ptr<infra::QSettingsColorProfileOverrideStore> overrideStore;
```

and construct the store where `Impl` is initialised:
`overrideStore = std::make_unique<infra::QSettingsColorProfileOverrideStore>();`

Rename `applyDefaultColorProfile` to keep its body, but have it end by calling `applyColorProfilePolicy()` instead of `viewportWidget->setDefaultColorProfile(...)`, caching the validated bytes in a new `Impl` member `std::vector<uint8_t> defaultProfileBytes;`. Then:

```cpp
void MainWindow::applyColorProfilePolicy()
{
    ColorProfilePolicy policy;
    policy.defaultBytes = m_impl->defaultProfileBytes;
    for (const auto& entry : m_impl->overrideStore->all()) {
        policy.overridePathsBySlideId[entry.slideId] = entry.profilePath;
    }
    m_impl->viewportWidget->setColorProfilePolicy(std::move(policy));
}

std::string MainWindow::currentSlideId() const
{
    if (!m_impl->viewportWidget->isSlideOpen()) {
        return {};
    }
    const core::SlideInfo& info = m_impl->viewportWidget->slideInfo();
    if (info.scenes.empty()) {
        return {};
    }
    uint64_t fileSize = 0;
    try {
        fileSize = static_cast<uint64_t>(
            std::filesystem::file_size(m_impl->viewportWidget->currentFilePath()));
    } catch (const std::exception& ex) {
        spdlog::warn("MainWindow::currentSlideId: {}", ex.what());
        return {};
    }
    return core::computeSlideId(info, fileSize);
}
```

- [ ] **Step 2: Create the actions**

In `createActions`, after the `clearDefaultProfileAction` block:

```cpp
        setSlideProfileAction = new QAction("Set ICC Profile for This Slide…", owner);
        setSlideProfileAction->setStatusTip(
            "Choose an RGB ICC profile for the slide on screen");
        setSlideProfileAction->setEnabled(false);

        clearSlideProfileAction = new QAction("Clear ICC Profile for This Slide", owner);
        clearSlideProfileAction->setStatusTip(
            "Stop overriding this slide's color profile");
        clearSlideProfileAction->setEnabled(false);
```

In `createMenus`, after `viewMenu->addAction(clearDefaultProfileAction);`:

```cpp
        viewMenu->addSeparator();
        viewMenu->addAction(setSlideProfileAction);
        viewMenu->addAction(clearSlideProfileAction);
```

In `connectSignals`, beside the existing default-profile connections:

```cpp
        QObject::connect(setSlideProfileAction, &QAction::triggered,
                         owner, &MainWindow::onSetSlideColorProfile);
        QObject::connect(clearSlideProfileAction, &QAction::triggered,
                         owner, &MainWindow::onClearSlideColorProfile);
```

Enable both wherever the colour-management action's enabled state is updated (around `MainWindow.cpp:396`): `setSlideProfileAction` follows `m_impl->viewportWidget->isSlideOpen()`, and `clearSlideProfileAction` additionally requires `m_impl->overrideStore->find(currentSlideId()).has_value()`. Disable both in the no-slide branch near `:462`.

- [ ] **Step 3: Implement setting an override**

```cpp
void MainWindow::onSetSlideColorProfile()
{
    const std::string slideId = currentSlideId();
    if (slideId.empty()) {
        QMessageBox::warning(this, tr("Slide ICC Profile"),
                             tr("This slide could not be identified, so a profile "
                                "cannot be remembered for it."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select ICC Profile for This Slide"), QString(),
        tr("ICC profiles (*.icc *.icm);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Slide ICC Profile"),
                             tr("Could not read %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    const QByteArray raw = file.readAll();
    const std::vector<uint8_t> bytes(raw.begin(), raw.end());

    const core::IccHeaderSummary summary = core::inspectIccHeader(bytes);
    if (!summary.plausible) {
        QMessageBox::warning(this, tr("Slide ICC Profile"),
                             tr("%1 is not a valid ICC profile.")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (summary.dataSpace != "RGB ") {
        QMessageBox::warning(
            this, tr("Slide ICC Profile"),
            tr("%1 describes %2 data. Color management needs an RGB profile.")
                .arg(QDir::toNativeSeparators(path),
                     QString::fromStdString(summary.dataSpace).trimmed()));
        return;
    }

    // An override displaces whatever the slide carries. When the slide carries
    // its own characterisation, say so before replacing it: the result is a
    // slide displayed through a profile its scanner did not produce.
    const core::SlideInfo& info = m_impl->viewportWidget->slideInfo();
    const bool displaces = info.colorProfileInfo.present;
    if (displaces) {
        const QString embedded = QString::fromStdString(info.colorProfileInfo.description);
        const auto answer = QMessageBox::question(
            this, tr("Slide ICC Profile"),
            tr("This slide embeds its own color profile (%1). Overriding it displays "
               "the slide through a profile the scanner did not produce.\n\nContinue?")
                .arg(embedded.isEmpty() ? tr("unnamed") : embedded),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    core::ColorProfileOverride entry;
    entry.slideId = slideId;
    entry.profilePath = path.toStdString();
    entry.slideDisplayName =
        QFileInfo(QString::fromStdString(m_impl->viewportWidget->currentFilePath()))
            .fileName().toStdString();
    entry.displacedEmbedded = displaces;
    m_impl->overrideStore->set(entry);

    applyColorProfilePolicy();

    // An override has no effect in Raw mode, so a menu item that visibly did
    // nothing would read as a bug. Turning it on is what the user asked for in
    // substance.
    if (!m_impl->colorManagementAction->isChecked()) {
        m_impl->colorManagementAction->setChecked(true);
    }

    m_impl->viewportWidget->reopenCurrentScene();
}
```

- [ ] **Step 4: Implement clearing an override**

```cpp
void MainWindow::onClearSlideColorProfile()
{
    const std::string slideId = currentSlideId();
    if (slideId.empty()) {
        return;
    }

    m_impl->overrideStore->remove(slideId);
    applyColorProfilePolicy();
    // Colour management stays as the user left it; only the profile changes.
    m_impl->viewportWidget->reopenCurrentScene();
}
```

Add the includes `<QFileInfo>`, `<filesystem>`, `"slideio/viewer/core/SlideId.h"`, `"slideio/viewer/infra/QSettingsColorProfileOverrideStore.h"` and `"slideio/viewer/ui/ColorProfilePolicy.h"` to `MainWindow.cpp` in the project-then-Qt-then-std order the file already uses.

- [ ] **Step 5: Surface a broken override**

Without this the problem string is computed, carried across threads and dropped — the
user sees a slide quietly rendered through the wrong profile with nothing said.

At `MainWindow.cpp:405`, the colour-management tooltip already prefers
`defaultProfileProblem` over the slide-centric reason. A broken *override* is more
specific still, so it wins over both. Replace that preference chain with:

```cpp
                // Most specific problem first. An override the user set for
                // this very slide explains the colours better than a global
                // default does, and either explains them better than a message
                // about what the slide does or does not embed.
                const std::string overrideProblem =
                    m_impl->viewportWidget->lastColorProfileProblem();
                if (!overrideProblem.empty()) {
                    reason = overrideProblem;
                } else if (!available && !defaultProfileProblem.empty()) {
                    reason = defaultProfileProblem;
                }
```

Note that an override problem is reported whether or not colour management is
available, unlike the default's problem: the user asked for a specific profile on a
specific slide and did not get it, which is worth saying even when the item is
enabled and the slide is rendering through its embedded profile perfectly well.

- [ ] **Step 6: Build and run the whole suite**

Run: `./build.sh && ctest --test-dir build/build -C Release --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Verify by hand**

Open a slide. Set a per-slide profile. Expect: colour management switches on, the slide reloads, and the image changes. Close and reopen the slide; expect the override still applied. Clear it; expect the image to revert.

Drive the GUI by maximising the window before clicking; the dock reopens on Associated Images, so dismiss or ignore that.

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/MainWindow.h src/ui/src/MainWindow.cpp
git commit -m "Set and clear a colour profile for the slide on screen"
```

---

### Task 7: `ui` — say which profile is in use

**Files:**
- Modify: `src/ui/src/SlidePropertiesPanel.cpp:219-260` (`rebuildColorProfileRows`)
- Modify: `src/ui/include/slideio/viewer/ui/SlidePropertiesPanel.h` if the panel needs the origin passed in
- Test: `tests/ui/SlidePropertiesPanelTest.cpp` (create if absent) or extend an existing UI test

**Interfaces:**
- Consumes: `SlideInfo::colorProfileOrigin`, `SlideInfo::displacedEmbeddedProfile` (Task 2, set in Task 4).
- Produces: nothing other tasks consume.

- [ ] **Step 1: Read the existing panel code**

Run: `sed -n '210,265p' src/ui/src/SlidePropertiesPanel.cpp`

The panel is refreshed from the *active* scene (see the comment at `MainWindow.cpp:845`), not from the `SlideInfo` captured at open, because the two report different provenance. The origin and the displaced flag live on `SlideInfo`, so the panel needs them passed alongside the active `ColorProfileInfo` rather than read from it.

- [ ] **Step 2: Write the failing test**

Add to `tests/ui/` a test over a pure helper that formats the two rows, so the assertion does not need a live panel:

```cpp
TEST_CASE("profile origin is named for the user", "[ui][SlideProperties]")
{
    using namespace slideio::viewer;

    REQUIRE(std::string(ui::colorProfileOriginName(core::ColorProfileOrigin::Library))
            == "Slide");
    REQUIRE(std::string(ui::colorProfileOriginName(core::ColorProfileOrigin::DefaultSetting))
            == "Default setting");
    REQUIRE(std::string(ui::colorProfileOriginName(core::ColorProfileOrigin::SlideOverride))
            == "Per-slide override");
}

TEST_CASE("a displaced embedded profile is called out", "[ui][SlideProperties]")
{
    using namespace slideio::viewer;

    core::SlideInfo info;
    info.colorProfileOrigin = core::ColorProfileOrigin::SlideOverride;
    info.displacedEmbeddedProfile = true;
    info.colorProfileInfo.present = true;
    info.colorProfileInfo.description = "Aperio GX";

    const std::string note = ui::displacedProfileNote(info);
    REQUIRE(note.find("Aperio GX") != std::string::npos);

    info.displacedEmbeddedProfile = false;
    REQUIRE(ui::displacedProfileNote(info).empty());
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `./build.sh`
Expected: FAIL at compile time — `colorProfileOriginName` and `displacedProfileNote` are not declared.

- [ ] **Step 4: Implement the two helpers and use them**

Declare both in `src/ui/include/slideio/viewer/ui/SlidePropertiesPanel.h` (free functions in `slideio::viewer::ui`), implement them in `SlidePropertiesPanel.cpp`:

```cpp
const char* colorProfileOriginName(core::ColorProfileOrigin origin)
{
    switch (origin) {
        case core::ColorProfileOrigin::Library:        return "Slide";
        case core::ColorProfileOrigin::DefaultSetting: return "Default setting";
        case core::ColorProfileOrigin::SlideOverride:  return "Per-slide override";
    }
    return "Slide";
}

std::string displacedProfileNote(const core::SlideInfo& info)
{
    if (!info.displacedEmbeddedProfile) {
        return {};
    }
    const std::string embedded =
        info.colorProfileInfo.description.empty() ? "unnamed" : info.colorProfileInfo.description;
    return "displaced: " + embedded;
}
```

In `rebuildColorProfileRows`, after the existing `Source` row built from
`colorProfileSourceName(info.source)`, add the two new rows. Match whatever row-adding
helper that function already uses — the call below is written as `addRow(label, value)`
and must be renamed to the real one:

```cpp
    // ColorProfileSource reports both a default and an override as "Supplied",
    // so the library's own answer cannot distinguish them. This row can.
    addRow(tr("Origin"),
           QString::fromLatin1(colorProfileOriginName(slideInfo.colorProfileOrigin)));

    const std::string displaced = displacedProfileNote(slideInfo);
    if (!displaced.empty()) {
        addRow(tr("Embedded"), QString::fromStdString(displaced));
    }
```

`rebuildColorProfileRows` currently takes only a `ColorProfileInfo`, which carries
neither field — the panel is refreshed from the *active* scene, whose profile info is
not the `SlideInfo` captured at open. Give the panel the two values alongside it:
extend `SlidePropertiesPanel::setActiveColorProfile` to take
`(const core::ColorProfileInfo&, core::ColorProfileOrigin, bool displacedEmbedded)`,
and update its one call site at `MainWindow.cpp:850` to pass
`m_impl->viewportWidget->slideInfo().colorProfileOrigin` and
`...slideInfo().displacedEmbeddedProfile`.

The result is that a slide shown through a non-native profile says so whenever the
panel is open, not only in the moment the override was set.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `./build.sh && ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/SlidePropertiesPanel.h \
        src/ui/src/SlidePropertiesPanel.cpp tests/ui/ tests/CMakeLists.txt
git commit -m "Name the profile a slide is being shown through"
```

---

### Task 8: `ui` — the manage dialog

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/SlideProfilesDialog.h`
- Create: `src/ui/src/SlideProfilesDialog.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Modify: `src/ui/src/MainWindow.cpp` (a third action, `Manage Slide ICC Profiles…`)
- Test: `tests/ui/SlideProfilesDialogTest.cpp`

**Interfaces:**
- Consumes: `IColorProfileOverrideStore` (Task 2), `classifyDefaultProfile` / `inspectIccHeader` (existing), `MainWindow::applyColorProfilePolicy` (Task 6).
- Produces: `SlideProfilesDialog`, constructed with an `IColorProfileOverrideStore&` so the dialog is testable against the fake from Task 2.

- [ ] **Step 1: Write the failing test for the row model**

The dialog's testable part is the row it builds for each entry, not its widgets:

```cpp
TEST_CASE("a row reports a missing profile file", "[ui][SlideProfiles]")
{
    using namespace slideio::viewer;

    core::ColorProfileOverride entry;
    entry.slideId = "a3f8c2e109b74d21";
    entry.profilePath = "//unreachable/never/here.icc";
    entry.slideDisplayName = "case001_HE.svs";
    entry.displacedEmbedded = true;

    const ui::SlideProfileRow row = ui::buildSlideProfileRow(entry);

    REQUIRE(row.slideName == "case001_HE.svs");
    REQUIRE(row.status == "File missing");
    REQUIRE(row.displacesEmbedded);
}

TEST_CASE("a row falls back to the slide id when the name is unknown",
          "[ui][SlideProfiles]")
{
    using namespace slideio::viewer;

    core::ColorProfileOverride entry;
    entry.slideId = "a3f8c2e109b74d21";
    entry.profilePath = "//unreachable/never/here.icc";

    const ui::SlideProfileRow row = ui::buildSlideProfileRow(entry);
    REQUIRE(row.slideName == "a3f8c2e109b74d21");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./build.sh`
Expected: FAIL at compile time — `SlideProfilesDialog.h: No such file or directory`.

- [ ] **Step 3: Write the row builder and the dialog**

In `SlideProfilesDialog.h`, declare:

```cpp
struct SlideProfileRow
{
    std::string slideName;      ///< the stored display name, or the id if none
    std::string profilePath;
    std::string status;         ///< "OK", "File missing", "Not a profile", "Not RGB"
    bool displacesEmbedded = false;
};

SlideProfileRow buildSlideProfileRow(const core::ColorProfileOverride& entry);

class SlideProfilesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SlideProfilesDialog(core::IColorProfileOverrideStore& store,
                                 QWidget* parent = nullptr);
    ~SlideProfilesDialog() override;

signals:
    /// Emitted after any removal, so MainWindow can rebuild the policy and
    /// reopen the slide on screen if its own entry went.
    void overridesChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

In `SlideProfilesDialog.cpp`:

```cpp
SlideProfileRow buildSlideProfileRow(const core::ColorProfileOverride& entry)
{
    SlideProfileRow row;
    row.slideName = entry.slideDisplayName.empty() ? entry.slideId : entry.slideDisplayName;
    row.profilePath = entry.profilePath;
    row.displacesEmbedded = entry.displacedEmbedded;

    QFile file(QString::fromStdString(entry.profilePath));
    const bool readable = file.open(QIODevice::ReadOnly);

    std::vector<uint8_t> bytes;
    if (readable) {
        const QByteArray raw = file.readAll();
        bytes.assign(raw.begin(), raw.end());
    }

    // The same classification the open path uses, so the dialog and the slide
    // can never disagree about whether a profile is usable.
    switch (core::classifyDefaultProfile(readable, core::inspectIccHeader(bytes))) {
        case core::DefaultProfileStatus::Ok:           row.status = "OK";            break;
        case core::DefaultProfileStatus::Unreadable:   row.status = "File missing";  break;
        case core::DefaultProfileStatus::NotAProfile:  row.status = "Not a profile"; break;
        case core::DefaultProfileStatus::NotRgb:       row.status = "Not RGB";       break;
    }
    return row;
}

struct SlideProfilesDialog::Impl
{
    SlideProfilesDialog* owner = nullptr;
    core::IColorProfileOverrideStore* store = nullptr;
    QTableWidget* table = nullptr;
    std::vector<std::string> rowSlideIds; // parallel to the table's rows

    void reload()
    {
        const std::vector<core::ColorProfileOverride> entries = store->all();
        rowSlideIds.clear();
        table->setRowCount(static_cast<int>(entries.size()));

        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const SlideProfileRow row = buildSlideProfileRow(entries[static_cast<size_t>(i)]);
            rowSlideIds.push_back(entries[static_cast<size_t>(i)].slideId);

            table->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(row.slideName)));
            table->setItem(i, 1, new QTableWidgetItem(
                QDir::toNativeSeparators(QString::fromStdString(row.profilePath))));
            table->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(row.status)));
            table->setItem(i, 3, new QTableWidgetItem(
                row.displacesEmbedded ? QObject::tr("displaces embedded") : QString()));
        }
    }

    void removeSelected()
    {
        const int selected = table->currentRow();
        if (selected < 0 || selected >= static_cast<int>(rowSlideIds.size())) {
            return;
        }
        store->remove(rowSlideIds[static_cast<size_t>(selected)]);
        reload();
        emit owner->overridesChanged();
    }

    void removeAll()
    {
        if (QMessageBox::question(
                owner, QObject::tr("Slide ICC Profiles"),
                QObject::tr("Remove every stored per-slide color profile?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        for (const auto& entry : store->all()) {
            store->remove(entry.slideId);
        }
        reload();
        emit owner->overridesChanged();
    }
};

SlideProfilesDialog::SlideProfilesDialog(core::IColorProfileOverrideStore& store,
                                         QWidget* parent)
    : QDialog(parent), m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;
    m_impl->store = &store;

    setWindowTitle(tr("Slide ICC Profiles"));
    resize(720, 360);

    m_impl->table = new QTableWidget(0, 4, this);
    m_impl->table->setHorizontalHeaderLabels(
        {tr("Slide"), tr("Profile"), tr("Status"), QString()});
    m_impl->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_impl->table->horizontalHeader()->setStretchLastSection(true);

    auto* removeBtn = new QPushButton(tr("Remove"), this);
    auto* removeAllBtn = new QPushButton(tr("Remove All"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);

    connect(removeBtn, &QPushButton::clicked, this, [this]() { m_impl->removeSelected(); });
    connect(removeAllBtn, &QPushButton::clicked, this, [this]() { m_impl->removeAll(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(removeBtn);
    buttons->addWidget(removeAllBtn);
    buttons->addStretch();
    buttons->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_impl->table);
    layout->addLayout(buttons);

    m_impl->reload();
}

SlideProfilesDialog::~SlideProfilesDialog() = default;
```

This follows the PIMPL idiom the other widget classes use. `buildSlideProfileRow` is a free function precisely so the tests above can reach it without constructing a dialog.

- [ ] **Step 4: Wire it into MainWindow**

Add a `manageSlideProfilesAction` beside the other two, always enabled, placed after `clearSlideProfileAction` in the View menu. Its handler constructs the dialog over `*m_impl->overrideStore`, connects `overridesChanged` to a lambda that calls `applyColorProfilePolicy()` and then `reopenCurrentScene()` when `currentSlideId()` no longer has an entry, and `exec()`s it.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `./build.sh && ctest --test-dir build/build -C Release --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Verify by hand**

Set overrides on two slides, open the dialog, confirm both are listed with status OK, remove one, confirm it is gone and the other remains.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/SlideProfilesDialog.h \
        src/ui/src/SlideProfilesDialog.cpp src/ui/CMakeLists.txt \
        src/ui/src/MainWindow.cpp tests/ui/SlideProfilesDialogTest.cpp tests/CMakeLists.txt
git commit -m "Let the user see and remove stored slide profiles"
```

---

### Task 9: Reconcile the design documents with what shipped

**Files:**
- Modify: `documents/03-software-architecture-and-design.md:731-748` (§9.1)
- Modify: `docs/superpowers/specs/2026-10-06-color-management-design.md:40-41` (the retired non-goal)
- Modify: `docs/superpowers/specs/2026-10-07-per-slide-color-profile-design.md` (status line)
- Modify: `documents/02-user-interface-design.md:146` (the stale deferred-sliders line)

Documentation only, no tests. It is its own task because it is the one a reviewer should be able to reject without rejecting working code.

- [ ] **Step 1: Replace the §9.1 identity scheme**

Replace the `generateSlideId` code block and its surrounding text with the implemented scheme: `computeSlideId(scenes, fileSizeBytes)`, FNV-1a over file size and the per-scene geometry table, 16 hex characters, explicitly non-cryptographic. State that the previous sketch hashed `scan_date` and `scanner_id`, which are not on `SlideInfo` and live only in inconsistently populated driver metadata.

Keep the surrounding claim that annotation files embed the ID and verify it on load — that is still the plan — but add that a matching ID is not proof of provenance and must not be treated as such.

- [ ] **Step 2: Retire the colour-management non-goal**

In `2026-10-06-color-management-design.md`, change the "No per-slide profile override" bullet to record that it was added on 2026-10-07 and point at this spec. Do not delete it: the reason it was once out of scope is still worth reading.

- [ ] **Step 3: Mark this spec implemented**

Change this feature's spec `**Status:**` from `Approved, not implemented` to `Implemented`, matching the convention the colour-management spec uses.

- [ ] **Step 4: Correct the stale sliders line**

`02-user-interface-design.md:146` says per-channel brightness/contrast sliders are "Deferred to a later phase". They shipped: `ChannelMixerPanel.cpp:194` has the intensity slider, and the panel has per-channel display min/max with a histogram view and Auto / Reset / Reset All. Replace the line with a description of what is there.

This is unrelated to the override feature and is in this task only because it is the same class of error — a design document that stopped matching the code — and fixing it while the documents are open costs nothing.

- [ ] **Step 5: Commit**

```bash
git add documents/03-software-architecture-and-design.md \
        documents/02-user-interface-design.md docs/superpowers/specs/
git commit -m "Reconcile the design documents with the shipped identity scheme"
```

---

## Verification

After Task 9, confirm the whole feature rather than its parts:

```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh && ctest --test-dir build/build -C Release --output-on-failure
```

Then, in the installed viewer:

1. Open a slide that embeds a profile. Set a per-slide override. Expect the warning naming the embedded profile, then colour management on, then a visible change.
2. Check the properties panel: Origin reads "Per-slide override" and an Embedded row names the displaced profile.
3. Close the slide, reopen it. The override still applies — this is the persistence claim.
4. Rename the slide file. Reopen it. The override still applies — this is the content-derived identity claim, and it is the one a path-keyed implementation would fail.
5. Open a *different* slide. The override does not apply to it.
6. Point the override at a file, delete that file, reopen the slide. The slide opens through its embedded profile, and the reason names the setting rather than the slide.
7. Open a multi-scene slide, set an override on scene 0, switch to scene 1. The override still applies — this is the file-level identity claim.
