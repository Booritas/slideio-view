# Workspace Change: Annotation Handling — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Changing the annotation workspace folder becomes an informed choice — copy, merge, keep what's on screen, use the new folder's, or cancel — instead of silently replacing what the user is looking at.

**Architecture:** Two pure functions in `core` (the merge, and the policy deciding which options are offered), one sequencer in `app` (`WorkspaceSwitch`, which owns the flush → capture → re-point → probe → ask → apply → `beginSlide` order and is testable against a fake repository), and one dialog plus one wiring change in `ui`. The switch no longer reopens the scene, so no pixel reloads and the `closeSlide()` path is out of this feature entirely.

**Tech Stack:** C++17, Qt 6.7.3 (Widgets only in `ui`), CMake + Conan 2, Catch2 v3, four ctest suites.

**Spec:** `docs/superpowers/specs/2026-10-15-workspace-change-design.md` (committed `5806f15`)

## Global Constraints

- Layering is strict and downward: `core` links **no Qt target at all**; `app` links core + Qt6::Core; `ui` links core + app + infra + Qt6::Widgets. `app` must never name `infra::JsonAnnotationRepository`.
- **No test binary creates a `QApplication`, so no test may construct a widget.** Anything needing a test lives in `core` or `app`.
- **Never run `app-tests`, `infra-tests` or `ui-tests` as a bare `.exe`** — it pops a modal Windows dialog that blocks until a human dismisses it. Always go through `ctest`. Only `core-tests` is safe to run directly.
- Every `.cpp` and every test file is listed explicitly in CMake. The only glob is for `ui` *headers*, for AUTOMOC. A `Q_OBJECT` header under `src/app/include` must additionally be listed in its target's sources.
- Build: `cmake --build build/build --config Release`. Test: `ctest --test-dir build/build -C Release --output-on-failure`.
- Naming: `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, 4-space indent, 120-column lines, `#pragma once`.
- `schemaVersion` stays at 1. No change to the file format or the file naming scheme.
- The old workspace is never written to beyond the flush it already receives, and never deleted from.
- Features land as direct commits to `main`. All AI-generated code is human-reviewed before commit.

## Review Focus

Five conditions the spec implies but does not name, each most likely to bite in the order listed. Each has a test in the task that owns the code.

1. **A resolver returning an option the policy did not offer** — e.g. `Merge` when the new folder's file is unparseable. Expected: no write happens; this codebase never writes over a file it could not read. (Task 3)
2. **No resolver wired at all** (`ui` forgot, or a non-interactive build). Expected: no write, no crash, the service still active afterwards. (Task 3)
3. **The write to the new root failing** (`NotWritable`). Expected: the result reports it, the old folder still holds the set, and `beginSlide` still runs so the slide keeps saving. (Task 3)
4. **An annotation whose `modifiedAt` is the epoch** — never stamped, or absent from a hand-edited file. Expected: it loses to any real timestamp rather than winning by accident. (Task 1)
5. **The same id appearing twice inside one input set** (a hand-edited file). Expected: the merge neither crashes nor silently drops annotations; the first occurrence is the one matched. (Task 1)

---

### Task 1: `core::mergeAnnotations`

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationMerge.h`
- Create: `src/core/src/AnnotationMerge.cpp`
- Create: `tests/core/AnnotationMergeTest.cpp`
- Modify: `src/core/CMakeLists.txt` (add `src/AnnotationMerge.cpp` to the source list)
- Modify: `tests/CMakeLists.txt` (add `core/AnnotationMergeTest.cpp` to the core-tests list)

**Interfaces:**
- Consumes: `core::Annotation` (`id()`, `metadata().modifiedAt`), `core::AnnotationMetadata`.
- Produces: `core::MergeOutcome { std::vector<Annotation> annotations; int adopted; int superseded; int kept; }` and `core::MergeOutcome core::mergeAnnotations(const std::vector<Annotation>& current, const std::vector<Annotation>& incoming)`.

- [ ] **Step 1: Write the failing test file**

Create `tests/core/AnnotationMergeTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationMerge.h"

#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer;

namespace
{

std::chrono::system_clock::time_point at(int64_t secondsSinceEpoch, int64_t millis = 0)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(secondsSinceEpoch)
                                                 + std::chrono::milliseconds(millis));
}

core::Annotation annotation(const std::string& id, int64_t modifiedSeconds, int64_t modifiedMillis = 0,
                            double x = 0.0)
{
    core::AnnotationProperties properties;
    properties.label = id;
    core::AnnotationMetadata metadata;
    metadata.createdAt = at(modifiedSeconds);
    metadata.modifiedAt = at(modifiedSeconds, modifiedMillis);
    return core::Annotation(id,
                            core::RectangleGeometry{core::PointF{x, 0.0}, core::PointF{x + 1.0, 1.0}},
                            properties, metadata);
}

std::vector<std::string> ids(const std::vector<core::Annotation>& annotations)
{
    std::vector<std::string> result;
    result.reserve(annotations.size());
    for (const auto& one : annotations) {
        result.push_back(one.id());
    }
    return result;
}

} // namespace

TEST_CASE("disjoint sets are unioned, current first", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 100), annotation("b", 100)};
    const std::vector<core::Annotation> incoming{annotation("c", 100), annotation("d", 100)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(ids(outcome.annotations) == std::vector<std::string>{"a", "b", "c", "d"});
    REQUIRE(outcome.adopted == 2);
    REQUIRE(outcome.superseded == 0);
    REQUIRE(outcome.kept == 0);
}

TEST_CASE("a newer incoming annotation supersedes in place", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 100), annotation("b", 100),
                                                annotation("c", 100)};
    const std::vector<core::Annotation> incoming{annotation("b", 200, 0, 50.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    // The slot is what matters: index order is paint order and hit-test order.
    REQUIRE(ids(outcome.annotations) == std::vector<std::string>{"a", "b", "c"});
    REQUIRE(outcome.annotations[1].boundingBox().x == 50.0);
    REQUIRE(outcome.superseded == 1);
    REQUIRE(outcome.kept == 0);
}

TEST_CASE("an older incoming annotation loses", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 200, 0, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 0, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations.size() == 1);
    REQUIRE(outcome.annotations[0].boundingBox().x == 7.0);
    REQUIRE(outcome.kept == 1);
    REQUIRE(outcome.superseded == 0);
}

TEST_CASE("an exact tie keeps the on-screen annotation", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 100, 0, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 0, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations[0].boundingBox().x == 7.0);
    REQUIRE(outcome.kept == 1);
}

TEST_CASE("timestamps differing only below the second compare equal", "[core][AnnotationMerge]")
{
    // formatIso8601Utc truncates to whole seconds (src/core/src/Iso8601.cpp:107),
    // so the sub-second part cannot survive a round-trip. Honouring it here would
    // make the same merge give a different answer before and after a save.
    const std::vector<core::Annotation> current{annotation("a", 100, 100, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 900, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations[0].boundingBox().x == 7.0);
    REQUIRE(outcome.kept == 1);
    REQUIRE(outcome.superseded == 0);
}

TEST_CASE("an unstamped annotation loses to a real timestamp", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> current{annotation("a", 0, 0, 7.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 100, 0, 99.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations[0].boundingBox().x == 99.0);
    REQUIRE(outcome.superseded == 1);
}

TEST_CASE("a repeated id inside one set drops nothing", "[core][AnnotationMerge]")
{
    // A hand-edited file can hold the same id twice. Neither copy is discarded
    // and neither is matched twice: the first occurrence is the one compared.
    const std::vector<core::Annotation> current{annotation("a", 100, 0, 1.0),
                                                annotation("a", 100, 0, 2.0)};
    const std::vector<core::Annotation> incoming{annotation("a", 300, 0, 3.0)};

    const core::MergeOutcome outcome = core::mergeAnnotations(current, incoming);

    REQUIRE(outcome.annotations.size() == 2);
    REQUIRE(outcome.annotations[0].boundingBox().x == 3.0);
    REQUIRE(outcome.annotations[1].boundingBox().x == 2.0);
    REQUIRE(outcome.superseded == 1);
}

TEST_CASE("empty inputs", "[core][AnnotationMerge]")
{
    const std::vector<core::Annotation> one{annotation("a", 100)};

    REQUIRE(core::mergeAnnotations({}, {}).annotations.empty());
    REQUIRE(core::mergeAnnotations(one, {}).annotations.size() == 1);
    REQUIRE(core::mergeAnnotations({}, one).annotations.size() == 1);
    REQUIRE(core::mergeAnnotations({}, one).adopted == 1);
}
```

- [ ] **Step 2: Add both files to CMake**

In `src/core/CMakeLists.txt`, add `src/AnnotationMerge.cpp` to the `add_library(slideio-viewer-core STATIC ...)` list, after `src/AnnotationMeasurement.cpp`.

In `tests/CMakeLists.txt`, add `core/AnnotationMergeTest.cpp` to the core-tests source list, after `core/AnnotationMeasurementTest.cpp`.

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build/build --config Release`
Expected: FAIL — `Cannot open include file: 'slideio/viewer/core/AnnotationMerge.h'`.

- [ ] **Step 4: Write the header**

Create `src/core/include/slideio/viewer/core/AnnotationMerge.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <vector>

namespace slideio::viewer::core
{

/// What a merge did, so the result can be reported rather than assumed.
struct MergeOutcome
{
    std::vector<Annotation> annotations;
    /// Ids only the incoming set had.
    int adopted = 0;
    /// Ids in both, where the incoming one was newer and replaced the other.
    int superseded = 0;
    /// Ids in both, where the current one was newer or the two tied.
    int kept = 0;
};

/// Unions two annotation sets by id.
///
/// Where both sets hold an id, the later `modifiedAt` survives. **The
/// comparison truncates both sides to whole seconds**, because that is the
/// precision the file format carries: formatIso8601Utc casts to
/// std::chrono::seconds, so an annotation that has been to disk has lost its
/// sub-second part and one still in memory has not. Comparing them as they are
/// would favour whichever happens to be in memory and would give a different
/// answer after a round-trip. Merging the same two sets twice must agree with
/// itself. On a tie `current` wins: it is what the user is looking at.
///
/// Order is preserved deliberately, because index order is paint order and
/// hit-test order. `current` keeps its sequence, a superseded annotation's
/// replacement takes the slot the old one held, and ids only `incoming` had are
/// appended.
///
/// An id repeated inside one input is left alone: both copies survive and only
/// the first is matched. That is malformed input, not a case to silently
/// "repair" by dropping a clinical finding.
///
/// Nothing is deduplicated by geometry. Two identical shapes under different
/// ids are two findings, possibly from two readers.
[[nodiscard]] MergeOutcome mergeAnnotations(const std::vector<Annotation>& current,
                                            const std::vector<Annotation>& incoming);

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Write the implementation**

Create `src/core/src/AnnotationMerge.cpp`:

```cpp
#include "slideio/viewer/core/AnnotationMerge.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace slideio::viewer::core
{

namespace
{

/// The precision the on-disk format carries. See the header.
std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
toWholeSeconds(std::chrono::system_clock::time_point t)
{
    return std::chrono::time_point_cast<std::chrono::seconds>(t);
}

} // namespace

MergeOutcome mergeAnnotations(const std::vector<Annotation>& current,
                              const std::vector<Annotation>& incoming)
{
    MergeOutcome outcome;
    outcome.annotations = current;

    // First occurrence wins the slot: a repeated id inside `current` leaves its
    // second copy in place, matched by nothing.
    std::unordered_map<std::string, std::size_t> slotById;
    for (std::size_t i = 0; i < outcome.annotations.size(); ++i) {
        slotById.emplace(outcome.annotations[i].id(), i);
    }

    for (const Annotation& candidate : incoming) {
        const auto found = slotById.find(candidate.id());
        if (found == slotById.end()) {
            slotById.emplace(candidate.id(), outcome.annotations.size());
            outcome.annotations.push_back(candidate);
            ++outcome.adopted;
            continue;
        }

        Annotation& held = outcome.annotations[found->second];
        if (toWholeSeconds(candidate.metadata().modifiedAt)
            > toWholeSeconds(held.metadata().modifiedAt)) {
            held = candidate;
            ++outcome.superseded;
        } else {
            ++outcome.kept;
        }
    }

    return outcome;
}

} // namespace slideio::viewer::core
```

- [ ] **Step 6: Build and run the core suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release -R core-tests --output-on-failure`
Expected: PASS, all assertions in the eight new TEST_CASEs green.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationMerge.h src/core/src/AnnotationMerge.cpp tests/core/AnnotationMergeTest.cpp src/core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add core::mergeAnnotations"
```

---

### Task 2: `core::workspaceSwitchOptions`

**Files:**
- Create: `src/core/include/slideio/viewer/core/WorkspaceSwitchPolicy.h`
- Create: `src/core/src/WorkspaceSwitchPolicy.cpp`
- Create: `tests/core/WorkspaceSwitchPolicyTest.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `core::WorkspaceSwitchFacts`, `core::WorkspaceSwitchOptions`, and `core::WorkspaceSwitchOptions core::workspaceSwitchOptions(const WorkspaceSwitchFacts&)`. Task 3 fills the facts and acts on the options; Task 4 renders both.

- [ ] **Step 1: Write the failing test file**

Create `tests/core/WorkspaceSwitchPolicyTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

using namespace slideio::viewer;

namespace
{

core::WorkspaceSwitchFacts onScreen(int count)
{
    core::WorkspaceSwitchFacts facts;
    facts.currentCount = count;
    facts.currentWritable = true;
    facts.oldPath = "/old/annotations/slide.s0.annotations.json";
    facts.newPath = "/new/annotations/slide.s0.annotations.json";
    return facts;
}

} // namespace

TEST_CASE("nothing on screen asks nothing", "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(0);

    SECTION("and no file in the new folder")
    {
        REQUIRE_FALSE(core::workspaceSwitchOptions(facts).ask);
    }

    SECTION("and a file in the new folder")
    {
        facts.newFileExists = true;
        facts.newCount = 4;
        // Nothing is at risk: there is nothing on screen to lose.
        REQUIRE_FALSE(core::workspaceSwitchOptions(facts).ask);
    }
}

TEST_CASE("on screen, new folder empty: copy or switch without copying",
          "[core][WorkspaceSwitchPolicy]")
{
    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(onScreen(12));

    REQUIRE(options.ask);
    REQUIRE(options.copy);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.merge);
    REQUIRE_FALSE(options.keepCurrent);
}

TEST_CASE("on screen, new folder has a readable file: merge, keep, or use new",
          "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.newFileExists = true;
    facts.newCount = 5;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.merge);
    REQUIRE(options.keepCurrent);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.copy);
}

TEST_CASE("an unparseable file in the new folder withholds every write",
          "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.newFileExists = true;
    facts.newFileReadable = false;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.copy);
    REQUIRE_FALSE(options.keepCurrent);
    REQUIRE_FALSE(options.merge);
}

TEST_CASE("read-only annotations are not offered for writing",
          "[core][WorkspaceSwitchPolicy]")
{
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.currentWritable = false;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.loadNew);
    REQUIRE_FALSE(options.copy);
    REQUIRE_FALSE(options.keepCurrent);
    REQUIRE_FALSE(options.merge);
}

TEST_CASE("a failed flush never withholds an option", "[core][WorkspaceSwitchPolicy]")
{
    // The user may be switching away precisely because the old folder could not
    // be written. Copy is the rescue, not a thing to take away.
    core::WorkspaceSwitchFacts facts = onScreen(12);
    facts.flushFailed = true;

    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);

    REQUIRE(options.ask);
    REQUIRE(options.copy);
}
```

- [ ] **Step 2: Add both files to CMake**

`src/core/CMakeLists.txt`: add `src/WorkspaceSwitchPolicy.cpp` after `src/AnnotationMerge.cpp`.
`tests/CMakeLists.txt`: add `core/WorkspaceSwitchPolicyTest.cpp` after `core/AnnotationMergeTest.cpp`.

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build/build --config Release`
Expected: FAIL — `Cannot open include file: 'slideio/viewer/core/WorkspaceSwitchPolicy.h'`.

- [ ] **Step 4: Write the header**

Create `src/core/include/slideio/viewer/core/WorkspaceSwitchPolicy.h`:

```cpp
#pragma once

#include <string>

namespace slideio::viewer::core
{

/// What is true at the moment the annotation workspace is about to change.
/// Gathered by app::WorkspaceSwitch after it has flushed to the old folder and
/// probed the new one, so every number here is measured rather than predicted.
struct WorkspaceSwitchFacts
{
    /// Annotations on screen, which is also what was just flushed to oldPath.
    int currentCount = 0;
    /// False when the open annotations are read-only: a slide-id mismatch
    /// answered ReadOnly, an unidentifiable slide, or an associated image.
    bool currentWritable = true;
    /// The flush to the old folder failed. Reported to the user, never used to
    /// withhold an option -- the user may be switching away because of it.
    bool flushFailed = false;
    std::string oldPath;

    bool newFileExists = false;
    /// False when the new folder's file is malformed, unreadable, or written by
    /// a newer schema version.
    bool newFileReadable = true;
    int newCount = 0;
    std::string newPath;
};

/// Which outcomes may be offered. Cancel is always available and is not listed.
///
/// `loadNew` means "use whatever the new folder has", which for an empty new
/// folder means switching to an empty set -- the dialog words it accordingly.
struct WorkspaceSwitchOptions
{
    /// False means no dialog at all: switch without asking.
    bool ask = false;
    bool copy = false;
    bool keepCurrent = false;
    bool merge = false;
    bool loadNew = false;
};

/// The matrix in the spec, as a function. Pure, so the decision can be tested
/// where the dialog that renders it cannot be.
///
/// Two rules carry the weight. Nothing on screen means nothing is at risk, so
/// there is nothing to ask about. And a file that could not be parsed is never
/// a file we write to, so copy, keep-current and merge are all withheld -- each
/// of them would write over contents nobody has read.
[[nodiscard]] WorkspaceSwitchOptions workspaceSwitchOptions(const WorkspaceSwitchFacts& facts);

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Write the implementation**

Create `src/core/src/WorkspaceSwitchPolicy.cpp`:

```cpp
#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

namespace slideio::viewer::core
{

WorkspaceSwitchOptions workspaceSwitchOptions(const WorkspaceSwitchFacts& facts)
{
    WorkspaceSwitchOptions options;

    if (facts.currentCount == 0) {
        // Nothing on screen to lose. The new folder's annotations, if any, just
        // load.
        return options;
    }

    options.ask = true;
    options.loadNew = true;

    if (!facts.currentWritable || !facts.newFileReadable) {
        return options;
    }

    if (facts.newFileExists) {
        options.keepCurrent = true;
        options.merge = true;
    } else {
        options.copy = true;
    }

    return options;
}

} // namespace slideio::viewer::core
```

- [ ] **Step 6: Build and run the core suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release -R core-tests --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/WorkspaceSwitchPolicy.h src/core/src/WorkspaceSwitchPolicy.cpp tests/core/WorkspaceSwitchPolicyTest.cpp src/core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add core::workspaceSwitchOptions"
```

---

### Task 3: `app::WorkspaceSwitch`

**Files:**
- Create: `src/app/include/slideio/viewer/app/WorkspaceSwitch.h`
- Create: `src/app/src/WorkspaceSwitch.cpp`
- Create: `tests/app/WorkspaceSwitchTest.cpp`
- Modify: `src/app/include/slideio/viewer/app/AnnotationPersistenceService.h` (add `documentCreatedAt()`)
- Modify: `src/app/src/AnnotationPersistenceService.cpp` (implement it)
- Modify: `src/app/CMakeLists.txt` (add `src/WorkspaceSwitch.cpp`)
- Modify: `tests/CMakeLists.txt` (add `app/WorkspaceSwitchTest.cpp`)

`WorkspaceSwitch` is **not** a `QObject` and has no signals, so its header is *not* added to the AUTOMOC list in `src/app/CMakeLists.txt` — only `src/WorkspaceSwitch.cpp` goes in the sources.

**Interfaces:**
- Consumes: `core::mergeAnnotations` / `core::MergeOutcome` (Task 1); `core::WorkspaceSwitchFacts`, `core::WorkspaceSwitchOptions`, `core::workspaceSwitchOptions` (Task 2); `app::AnnotationModel::annotations()`; `app::AnnotationPersistenceService::{isActive, endSlide, beginSlide}`; `core::IAnnotationRepository::{load, save, pathFor}`.
- Produces: `app::WorkspaceChoice`, `app::WorkspaceSwitchResult`, `app::WorkspaceSwitch` with `setResolver(Resolver)` and `run(newRoot, key, provenance)`. Task 5 calls them.

- [ ] **Step 1: Add `documentCreatedAt()` to the persistence service**

In `src/app/include/slideio/viewer/app/AnnotationPersistenceService.h`, after `isActive()`:

```cpp
    /// When the open document's history began: the loaded file's createdAt, or
    /// the moment this slide was begun if there was no file. A workspace switch
    /// reads it BEFORE its flush, so that endSlide() clearing more state in
    /// future cannot quietly change what a merged document claims.
    [[nodiscard]] std::chrono::system_clock::time_point documentCreatedAt() const;
```

In `src/app/src/AnnotationPersistenceService.cpp`, beside `isActive()`:

```cpp
std::chrono::system_clock::time_point AnnotationPersistenceService::documentCreatedAt() const
{
    return m_createdAt;
}
```

- [ ] **Step 2: Write the failing test file**

Create `tests/app/WorkspaceSwitchTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AnnotationPersistenceService.h"
#include "slideio/viewer/app/WorkspaceSwitch.h"
#include "slideio/viewer/core/AnnotationRepository.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace slideio::viewer;

namespace
{

/// Root-aware, unlike the FakeRepository in AnnotationPersistenceServiceTest.cpp:
/// a save made under one root must be invisible to a load from another, which is
/// the whole point of what is under test here.
class FakeRootRepository : public core::IAnnotationRepository
{
public:
    std::string root = "/old";
    /// Set to anything but Loaded to make the NEXT load from `root` fail.
    core::LoadStatus forcedStatus = core::LoadStatus::Loaded;
    std::string forcedForRoot;
    core::SaveStatus nextSave = core::SaveStatus::Saved;

    std::map<std::pair<std::string, std::string>, core::AnnotationDocument> stored;
    std::vector<std::pair<std::string, std::string>> saves;  ///< (root, path), in order
    std::vector<std::pair<std::string, std::string>> loads;  ///< (root, path), in order

    core::LoadResult load(const core::AnnotationKey& key) override
    {
        loads.emplace_back(root, pathFor(key));
        core::LoadResult result;
        result.path = pathFor(key);
        if (!forcedForRoot.empty() && forcedForRoot == root) {
            result.status = forcedStatus;
            result.message = "forced";
            return result;
        }
        const auto found = stored.find({root, keyText(key)});
        if (found == stored.end()) {
            result.status = core::LoadStatus::NotFound;
            return result;
        }
        result.status = core::LoadStatus::Loaded;
        result.document = found->second;
        return result;
    }

    core::SaveResult save(const core::AnnotationDocument& document) override
    {
        const core::AnnotationKey key{document.slideId, document.sceneIndex};
        saves.emplace_back(root, pathFor(key));
        core::SaveResult result;
        result.status = nextSave;
        result.path = pathFor(key);
        if (nextSave == core::SaveStatus::Saved) {
            stored[{root, keyText(key)}] = document;
        }
        return result;
    }

    [[nodiscard]] std::string pathFor(const core::AnnotationKey& key) const override
    {
        return root + "/annotations/" + keyText(key) + ".annotations.json";
    }

    [[nodiscard]] static std::string keyText(const core::AnnotationKey& key)
    {
        return key.slideId + ".s" + std::to_string(key.sceneIndex);
    }
};

core::SlideProvenance provenance()
{
    core::SlideProvenance p;
    p.fileName = "slide.svs";
    p.path = "/slides/slide.svs";
    return p;
}

core::Annotation annotation(const std::string& id, int64_t modifiedSeconds)
{
    core::AnnotationProperties properties;
    core::AnnotationMetadata metadata;
    metadata.createdAt = std::chrono::system_clock::time_point(std::chrono::seconds(modifiedSeconds));
    metadata.modifiedAt = metadata.createdAt;
    return core::Annotation(id, core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}},
                            properties, metadata);
}

const core::AnnotationKey kKey{"slide-1", 0};

/// A switch harness wired the way MainWindow wires it.
struct Harness
{
    FakeRootRepository repository;
    app::AnnotationModel model;
    app::AnnotationPersistenceService persistence{repository, model};
    app::WorkspaceSwitch sut{repository, model, persistence,
                             [this]() { return repository.root; },
                             [this](std::string root) { repository.root = std::move(root); }};

    void openWith(std::vector<core::Annotation> annotations)
    {
        core::AnnotationDocument document;
        document.slideId = kKey.slideId;
        document.sceneIndex = kKey.sceneIndex;
        document.annotations = std::move(annotations);
        repository.stored[{"/old", FakeRootRepository::keyText(kKey)}] = document;
        persistence.beginSlide(kKey, provenance());
    }
};

} // namespace

TEST_CASE("the flush reaches the old root before the probe reaches the new one",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.model.add(core::RectangleGeometry{core::PointF{5.0, 5.0}, core::PointF{6.0, 6.0}});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::LoadNew;
    });

    h.sut.run("/new", kKey, provenance());

    REQUIRE_FALSE(h.repository.saves.empty());
    REQUIRE(h.repository.saves.front().first == "/old");
    REQUIRE_FALSE(h.repository.loads.empty());
    REQUIRE(h.repository.loads.back().first == "/new");
}

TEST_CASE("copy writes the on-screen set to the new root", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts& facts,
                         const core::WorkspaceSwitchOptions& options) {
        REQUIRE(options.copy);
        REQUIRE(facts.currentCount == 2);
        REQUIRE_FALSE(facts.newFileExists);
        return app::WorkspaceChoice::Copy;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::Saved);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 1);
    REQUIRE(h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}].annotations.size() == 2);
    REQUIRE(h.model.annotations().size() == 2);
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("merge writes the merged document and reports the counts",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});

    core::AnnotationDocument inNew;
    inNew.slideId = kKey.slideId;
    inNew.annotations = {annotation("b", 300), annotation("c", 100)};
    h.repository.stored[{"/new", FakeRootRepository::keyText(kKey)}] = inNew;

    h.sut.setResolver([](const core::WorkspaceSwitchFacts& facts,
                         const core::WorkspaceSwitchOptions& options) {
        REQUIRE(options.merge);
        REQUIRE(facts.newCount == 2);
        return app::WorkspaceChoice::Merge;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.merge.adopted == 1);
    REQUIRE(result.merge.superseded == 1);
    REQUIRE(result.merge.kept == 0);
    REQUIRE(h.model.annotations().size() == 3);
}

TEST_CASE("use-the-new-folder's writes nothing", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::LoadNew;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::NothingToDo);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
    REQUIRE(h.model.annotations().empty());
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("cancel re-points back and leaves the slide saving", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100), annotation("b", 100)});
    h.sut.setResolver([](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        return app::WorkspaceChoice::Cancel;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.choice == app::WorkspaceChoice::Cancel);
    REQUIRE(h.repository.root == "/old");
    REQUIRE(h.model.annotations().size() == 2);
    // The failure this catches is invisible until the next edit vanishes:
    // step 1's endSlide() deactivated the service, and only a beginSlide
    // revives it.
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("an unparseable file in the new root is never written over",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.repository.forcedForRoot = "/new";
    h.repository.forcedStatus = core::LoadStatus::Malformed;

    bool asked = false;
    h.sut.setResolver([&asked](const core::WorkspaceSwitchFacts& facts,
                               const core::WorkspaceSwitchOptions& options) {
        asked = true;
        REQUIRE_FALSE(facts.newFileReadable);
        REQUIRE_FALSE(options.merge);
        REQUIRE_FALSE(options.keepCurrent);
        REQUIRE_FALSE(options.copy);
        // A resolver returning an option that was not offered is a ui bug, and
        // acting on it would write over a file nobody could read.
        return app::WorkspaceChoice::Merge;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(asked);
    REQUIRE(result.write == core::SaveStatus::NothingToDo);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
}

TEST_CASE("a failed flush still allows the copy that rescues the work",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.model.add(core::RectangleGeometry{core::PointF{5.0, 5.0}, core::PointF{6.0, 6.0}});
    h.repository.nextSave = core::SaveStatus::NotWritable;

    h.sut.setResolver([](const core::WorkspaceSwitchFacts& facts,
                         const core::WorkspaceSwitchOptions& options) {
        REQUIRE(facts.flushFailed);
        REQUIRE(options.copy);
        return app::WorkspaceChoice::Copy;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    // The write was attempted against the new root and reported honestly.
    REQUIRE(result.write == core::SaveStatus::NotWritable);
    REQUIRE(h.repository.saves.back().first == "/new");
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("a rescued flush abandons the switch", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});
    h.model.add(core::RectangleGeometry{core::PointF{5.0, 5.0}, core::PointF{6.0, 6.0}});
    h.repository.nextSave = core::SaveStatus::NotWritable;

    // Stands in for ui's save-failure dialog, which can re-point the workspace
    // to a folder the user picks while the failure is on screen.
    h.persistence.setSaveFailureResolver(
        [&h](const core::SaveResult&, int) {
            h.repository.root = "/rescue";
            h.repository.nextSave = core::SaveStatus::Saved;
            return app::AnnotationPersistenceService::SaveFailureChoice::Retry;
        });

    bool asked = false;
    h.sut.setResolver([&asked](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        asked = true;
        return app::WorkspaceChoice::Copy;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.rescued);
    REQUIRE_FALSE(asked);
    REQUIRE(h.repository.root == "/rescue");
    // Nothing was probed or written in the folder the dialog asked for.
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("no dialog when nothing is on screen", "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({});
    bool asked = false;
    h.sut.setResolver([&asked](const core::WorkspaceSwitchFacts&, const core::WorkspaceSwitchOptions&) {
        asked = true;
        return app::WorkspaceChoice::Cancel;
    });

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE_FALSE(asked);
    REQUIRE_FALSE(result.asked);
    REQUIRE(h.repository.root == "/new");
    REQUIRE(h.persistence.isActive());
}

TEST_CASE("no resolver writes nothing and still leaves the slide saving",
          "[app][WorkspaceSwitch]")
{
    Harness h;
    h.openWith({annotation("a", 100)});

    const app::WorkspaceSwitchResult result = h.sut.run("/new", kKey, provenance());

    REQUIRE(result.write == core::SaveStatus::NothingToDo);
    REQUIRE(h.repository.stored.count({"/new", FakeRootRepository::keyText(kKey)}) == 0);
    REQUIRE(h.persistence.isActive());
}
```

- [ ] **Step 3: Add the source and test to CMake**

`src/app/CMakeLists.txt`: add `src/WorkspaceSwitch.cpp` after `src/AnnotationEditing.cpp`. Do **not** add the header to the AUTOMOC list — it has no `Q_OBJECT`.
`tests/CMakeLists.txt`: add `app/WorkspaceSwitchTest.cpp` after `app/AnnotationEditingTest.cpp`.

- [ ] **Step 4: Run the test to verify it fails**

Run: `cmake --build build/build --config Release`
Expected: FAIL — `Cannot open include file: 'slideio/viewer/app/WorkspaceSwitch.h'`.

- [ ] **Step 5: Write the header**

Create `src/app/include/slideio/viewer/app/WorkspaceSwitch.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationMerge.h"
#include "slideio/viewer/core/AnnotationRepository.h"
#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

#include <functional>
#include <string>

namespace slideio::viewer::app
{

class AnnotationModel;
class AnnotationPersistenceService;

enum class WorkspaceChoice
{
    /// Write the on-screen set into a new folder that has no file for this key.
    Copy,
    /// Write the on-screen set over the file the new folder already has.
    KeepCurrent,
    Merge,
    /// Take whatever the new folder holds, writing nothing.
    LoadNew,
    Cancel,
};

struct WorkspaceSwitchResult
{
    WorkspaceChoice choice = WorkspaceChoice::Cancel;
    /// False when the facts did not warrant a dialog.
    bool asked = false;
    /// The save-failure resolver rescued the flush to a folder the user picked
    /// mid-failure, and made that folder the workspace. It beats the one typed
    /// into Preferences: the switch is abandoned, with no probe and no dialog.
    bool rescued = false;
    /// How step 1's flush to the old folder went, so the caller can say so.
    core::SaveStatus flush = core::SaveStatus::NothingToDo;
    /// Zeroed unless choice == Merge.
    core::MergeOutcome merge;
    core::SaveStatus write = core::SaveStatus::NothingToDo;
    /// Empty when nothing was written.
    std::string writePath;
};

/// Moves the open slide's annotations across a change of workspace folder.
///
/// The order is the design, and it is the reason this lives in app rather than
/// in the dialog's caller: every step of it is testable here, and none of it is
/// testable in a widget.
///
///   1. flush to the OLD root, so what is on screen is also on disk there
///   2. capture the in-memory set
///   3. re-point to the new root and load() -- that load is the probe
///   4. build the facts from the capture and the probe
///   5. ask the resolver, which is the dialog
///   6. apply the choice, writing at most once
///   7. beginSlide against the root now in effect
///
/// Step 7 is not optional on any path, cancel included: step 1 deactivated the
/// service, and a slide left open with a deactivated service stops saving
/// silently.
///
/// The repository's root is reached through callbacks rather than through the
/// repository interface: core::IAnnotationRepository exposes only load, save and
/// pathFor, and a filesystem root does not belong on it -- a remote repository
/// has none.
class WorkspaceSwitch
{
public:
    using Resolver = std::function<WorkspaceChoice(const core::WorkspaceSwitchFacts&,
                                                   const core::WorkspaceSwitchOptions&)>;

    WorkspaceSwitch(core::IAnnotationRepository& repository, AnnotationModel& model,
                    AnnotationPersistenceService& persistence,
                    std::function<std::string()> currentRoot,
                    std::function<void(std::string)> repointTo);

    void setResolver(Resolver resolver);

    /// `key` and `provenance` describe the slide that is open. With no resolver
    /// set, nothing is written: this never guesses at the user's expense.
    WorkspaceSwitchResult run(const std::string& newRoot, const core::AnnotationKey& key,
                              const core::SlideProvenance& provenance);

private:
    core::IAnnotationRepository& m_repository;
    AnnotationModel& m_model;
    AnnotationPersistenceService& m_persistence;
    std::function<std::string()> m_currentRoot;
    std::function<void(std::string)> m_repointTo;
    Resolver m_resolver;
};

} // namespace slideio::viewer::app
```

- [ ] **Step 6: Write the implementation**

Create `src/app/src/WorkspaceSwitch.cpp`:

```cpp
#include "slideio/viewer/app/WorkspaceSwitch.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AnnotationPersistenceService.h"

#include <chrono>
#include <utility>
#include <vector>

namespace slideio::viewer::app
{

namespace
{

bool wasOffered(const core::WorkspaceSwitchOptions& options, WorkspaceChoice choice)
{
    switch (choice) {
    case WorkspaceChoice::Copy:
        return options.copy;
    case WorkspaceChoice::KeepCurrent:
        return options.keepCurrent;
    case WorkspaceChoice::Merge:
        return options.merge;
    case WorkspaceChoice::LoadNew:
    case WorkspaceChoice::Cancel:
        return true;
    }
    return false;
}

/// The set's history starts at the earlier of the two documents'. A zero
/// time_point means "unknown", not "the epoch", so it never wins.
std::chrono::system_clock::time_point earlierCreatedAt(std::chrono::system_clock::time_point a,
                                                       std::chrono::system_clock::time_point b)
{
    const bool aKnown = a.time_since_epoch().count() != 0;
    const bool bKnown = b.time_since_epoch().count() != 0;
    if (!aKnown) {
        return b;
    }
    if (!bKnown) {
        return a;
    }
    return a < b ? a : b;
}

} // namespace

WorkspaceSwitch::WorkspaceSwitch(core::IAnnotationRepository& repository, AnnotationModel& model,
                                 AnnotationPersistenceService& persistence,
                                 std::function<std::string()> currentRoot,
                                 std::function<void(std::string)> repointTo)
    : m_repository(repository)
    , m_model(model)
    , m_persistence(persistence)
    , m_currentRoot(std::move(currentRoot))
    , m_repointTo(std::move(repointTo))
{
}

void WorkspaceSwitch::setResolver(Resolver resolver)
{
    m_resolver = std::move(resolver);
}

WorkspaceSwitchResult WorkspaceSwitch::run(const std::string& newRoot,
                                           const core::AnnotationKey& key,
                                           const core::SlideProvenance& provenance)
{
    WorkspaceSwitchResult result;
    const std::string oldRoot = m_currentRoot();

    core::WorkspaceSwitchFacts facts;
    // Read before the flush: endSlide() deactivates the service, and reading
    // either of these afterwards would describe the switch rather than the
    // slide.
    facts.currentWritable = m_persistence.isActive();
    const std::chrono::system_clock::time_point currentCreatedAt = m_persistence.documentCreatedAt();
    facts.oldPath = m_repository.pathFor(key);

    // 1. Flush to the old root.
    const core::SaveResult flushed = m_persistence.endSlide();
    result.flush = flushed.status;
    facts.flushFailed = flushed.status == core::SaveStatus::NotWritable
                        || flushed.status == core::SaveStatus::Failed;

    // The flush can fail, and the save-failure resolver may rescue it to a
    // folder the user picks mid-failure -- which it makes the workspace on the
    // way out. That is a more recent choice than the one typed into
    // Preferences, and the work has already been written there. Re-pointing
    // somewhere else now would strand the file it just saved in a folder this
    // application never looks at again.
    if (m_currentRoot() != oldRoot) {
        result.rescued = true;
        result.choice = WorkspaceChoice::Cancel;
        m_persistence.beginSlide(key, provenance);
        return result;
    }

    // 2. Capture what is on screen.
    const std::vector<core::Annotation> current = m_model.annotations();
    facts.currentCount = static_cast<int>(current.size());

    // 3. Re-point, then probe. Everything written from here lands in the new
    // folder, which is the point.
    m_repointTo(newRoot);
    const core::LoadResult probe = m_repository.load(key);
    facts.newPath = probe.path.empty() ? m_repository.pathFor(key) : probe.path;
    switch (probe.status) {
    case core::LoadStatus::Loaded:
        facts.newFileExists = true;
        facts.newCount = static_cast<int>(probe.document.annotations.size());
        break;
    case core::LoadStatus::NotFound:
        break;
    case core::LoadStatus::Unreadable:
    case core::LoadStatus::Malformed:
    case core::LoadStatus::UnsupportedVersion:
        facts.newFileExists = true;
        facts.newFileReadable = false;
        break;
    }

    // 4-5. Decide what may be offered, then ask.
    const core::WorkspaceSwitchOptions options = core::workspaceSwitchOptions(facts);
    result.asked = options.ask;
    if (!options.ask || !m_resolver) {
        // Nothing at risk, or nobody to ask. Either way the outcome that writes
        // nothing is the honest one.
        result.choice = WorkspaceChoice::LoadNew;
    } else {
        result.choice = m_resolver(facts, options);
    }

    // 6. Apply.
    if (result.choice == WorkspaceChoice::Cancel) {
        m_repointTo(oldRoot);
        m_persistence.beginSlide(key, provenance);
        return result;
    }

    std::vector<core::Annotation> toWrite;
    bool write = false;
    if (wasOffered(options, result.choice)) {
        switch (result.choice) {
        case WorkspaceChoice::Copy:
        case WorkspaceChoice::KeepCurrent:
            toWrite = current;
            write = true;
            break;
        case WorkspaceChoice::Merge:
            result.merge = core::mergeAnnotations(current, probe.document.annotations);
            toWrite = result.merge.annotations;
            write = true;
            break;
        case WorkspaceChoice::LoadNew:
        case WorkspaceChoice::Cancel:
            break;
        }
    }

    if (write) {
        core::AnnotationDocument document;
        document.schemaVersion = core::kCurrentSchemaVersion;
        document.slideId = key.slideId;
        document.sceneIndex = key.sceneIndex;
        document.slide = provenance;
        document.createdAt = earlierCreatedAt(currentCreatedAt, probe.document.createdAt);
        document.modifiedAt = std::chrono::system_clock::now();
        document.annotations = std::move(toWrite);

        const core::SaveResult saved = m_repository.save(document);
        result.write = saved.status;
        result.writePath = saved.path;
    }

    // 7. Re-enter the slide against whichever root is now in effect. The file is
    // the source of truth; the model is never hand-patched.
    m_persistence.beginSlide(key, provenance);
    return result;
}

} // namespace slideio::viewer::app
```

- [ ] **Step 7: Build and run the app suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release -R app-tests --output-on-failure`
Expected: PASS. **Do not run `slideio-viewer-app-tests.exe` directly** — it pops a modal dialog.

- [ ] **Step 8: Run every suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: 4/4 pass. `documentCreatedAt()` touched a shared header, so the whole set matters here.

- [ ] **Step 9: Commit**

```bash
git add src/app/include/slideio/viewer/app/WorkspaceSwitch.h src/app/src/WorkspaceSwitch.cpp src/app/include/slideio/viewer/app/AnnotationPersistenceService.h src/app/src/AnnotationPersistenceService.cpp tests/app/WorkspaceSwitchTest.cpp src/app/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add app::WorkspaceSwitch"
```

---

### Task 4: `ui::WorkspaceChangeDialog`

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/WorkspaceChangeDialog.h`
- Create: `src/ui/src/WorkspaceChangeDialog.cpp`
- Modify: `src/ui/CMakeLists.txt` (add `src/WorkspaceChangeDialog.cpp` to the explicit source list; the header is picked up by the existing header glob for AUTOMOC)

**Interfaces:**
- Consumes: `core::WorkspaceSwitchFacts`, `core::WorkspaceSwitchOptions` (Task 2); `app::WorkspaceChoice` (Task 3).
- Produces: `ui::WorkspaceChangeDialog(facts, options, parent)` with `[[nodiscard]] app::WorkspaceChoice choice() const`, meaningful after `exec()`.

No test: this is a widget, and no test binary in this repo creates a `QApplication`. It is covered by group X in the spec. Keep every decision out of it — it renders the two structs and returns the enum.

- [ ] **Step 1: Write the header**

Create `src/ui/include/slideio/viewer/ui/WorkspaceChangeDialog.h`:

```cpp
#pragma once

#include "slideio/viewer/app/WorkspaceSwitch.h"
#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

#include <QDialog>

namespace slideio::viewer::ui
{

/// Asks what should happen to the open slide's annotations when the annotation
/// workspace folder changes.
///
/// Holds no policy: which buttons exist comes from core::workspaceSwitchOptions,
/// and every number it shows was measured by app::WorkspaceSwitch. A QMessageBox
/// would do for two buttons; this has up to four, each needing a line of
/// explanation under it.
class WorkspaceChangeDialog : public QDialog
{
    Q_OBJECT

public:
    WorkspaceChangeDialog(const core::WorkspaceSwitchFacts& facts,
                          const core::WorkspaceSwitchOptions& options, QWidget* parent = nullptr);

    /// Meaningful after exec(). Rejecting the dialog is Cancel.
    [[nodiscard]] app::WorkspaceChoice choice() const;

private:
    app::WorkspaceChoice m_choice = app::WorkspaceChoice::Cancel;
};

} // namespace slideio::viewer::ui
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/src/WorkspaceChangeDialog.cpp`:

```cpp
#include "slideio/viewer/ui/WorkspaceChangeDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

namespace
{

QString nativePath(const std::string& path)
{
    return QDir::toNativeSeparators(QString::fromStdString(path));
}

} // namespace

WorkspaceChangeDialog::WorkspaceChangeDialog(const core::WorkspaceSwitchFacts& facts,
                                             const core::WorkspaceSwitchOptions& options,
                                             QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Change annotation folder"));

    auto* layout = new QVBoxLayout(this);

    auto* headline = new QLabel(
        facts.newFileExists
            ? tr("The new annotation folder already has annotations for this slide.")
            : tr("The new annotation folder has no annotations for this slide."),
        this);
    headline->setWordWrap(true);
    QFont bold = headline->font();
    bold.setBold(true);
    headline->setFont(bold);
    layout->addWidget(headline);

    QString detail = tr("On screen now: %1 — saved in %2")
                         .arg(facts.currentCount)
                         .arg(nativePath(facts.oldPath));
    if (facts.newFileExists && facts.newFileReadable) {
        detail += tr("\nIn the new folder: %1 — %2").arg(facts.newCount).arg(nativePath(facts.newPath));
    } else if (facts.newFileExists) {
        detail += tr("\nThe new folder's file could not be read, so it will not be written to: %1")
                      .arg(nativePath(facts.newPath));
    }
    if (facts.flushFailed) {
        detail += tr("\nThe current folder could not be written. The annotations on screen are "
                     "still here, and copying them is how they are kept.");
    }
    if (!facts.currentWritable) {
        detail += tr("\nThe annotations on screen are open read-only and are not carried over.");
    }
    auto* detailLabel = new QLabel(detail, this);
    detailLabel->setWordWrap(true);
    detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(detailLabel);

    auto* explanation = new QLabel(
        tr("Annotations are read from the folder in use, so changing it replaces what you see. "
           "Nothing is deleted from the current folder. Only the scene you are viewing is carried "
           "over; other scenes and other slides stay where they are. Undo history is cleared."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* buttons = new QDialogButtonBox(this);

    const auto addChoice = [this, buttons](const QString& text, app::WorkspaceChoice choice,
                                           QDialogButtonBox::ButtonRole role) {
        QPushButton* button = buttons->addButton(text, role);
        connect(button, &QPushButton::clicked, this, [this, choice]() {
            m_choice = choice;
            accept();
        });
        return button;
    };

    QPushButton* preferred = nullptr;
    if (options.merge) {
        preferred = addChoice(tr("Merge"), app::WorkspaceChoice::Merge,
                              QDialogButtonBox::AcceptRole);
    }
    if (options.copy) {
        preferred = addChoice(tr("Copy to the new folder"), app::WorkspaceChoice::Copy,
                              QDialogButtonBox::AcceptRole);
    }
    if (options.keepCurrent) {
        QPushButton* keep = addChoice(tr("Keep what's on screen"), app::WorkspaceChoice::KeepCurrent,
                                      QDialogButtonBox::AcceptRole);
        keep->setToolTip(tr("Replaces the %1 annotations in the new folder's file.")
                             .arg(facts.newCount));
    }
    if (options.loadNew) {
        addChoice(facts.newFileExists ? tr("Use the new folder's") : tr("Switch without copying"),
                  app::WorkspaceChoice::LoadNew, QDialogButtonBox::DestructiveRole);
    }

    QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    connect(cancel, &QPushButton::clicked, this, [this]() {
        m_choice = app::WorkspaceChoice::Cancel;
        reject();
    });

    if (preferred != nullptr) {
        preferred->setDefault(true);
    }

    layout->addWidget(buttons);
}

app::WorkspaceChoice WorkspaceChangeDialog::choice() const
{
    return m_choice;
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 3: Add the source to CMake**

In `src/ui/CMakeLists.txt`, add `src/WorkspaceChangeDialog.cpp` to the explicit source list, beside the other dialogs.

- [ ] **Step 4: Build**

Run: `cmake --build build/build --config Release`
Expected: builds clean, no warnings from `set_project_warnings`.

- [ ] **Step 5: Run every suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: 4/4 pass — nothing should have changed, and this proves the new translation unit did not break the ui target's link.

- [ ] **Step 6: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/WorkspaceChangeDialog.h src/ui/src/WorkspaceChangeDialog.cpp src/ui/CMakeLists.txt
git commit -m "Add the workspace-change dialog"
```

---

### Task 5: Wire the switch into Preferences

**Files:**
- Modify: `src/ui/src/MainWindow.cpp` — `MainWindow::Impl::openPreferences()` (~line 1462), the `Impl` member list, and the construction site where `annotationPersistence` is built
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h` — add `currentSceneIndex()`
- Modify: `src/ui/src/ViewportWidget.cpp` — implement it

**Interfaces:**
- Consumes: `app::WorkspaceSwitch`, `app::WorkspaceChoice`, `app::WorkspaceSwitchResult` (Task 3); `ui::WorkspaceChangeDialog` (Task 4).
- Produces: nothing further.

- [ ] **Step 1: Expose the open scene index**

The annotation key is (slideId, sceneIndex) and `ViewportWidget` is the only holder of the second half. In `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, beside `currentSlideId()`:

```cpp
    /// The scene this viewport is showing. Half of the annotation key: a
    /// multi-scene file's scenes have one slide id between them.
    [[nodiscard]] int currentSceneIndex() const;
```

In `src/ui/src/ViewportWidget.cpp`, beside `currentSlideId()`:

```cpp
int ViewportWidget::currentSceneIndex() const
{
    return m_impl->currentSceneIndex;
}
```

- [ ] **Step 2: Own a `WorkspaceSwitch` in `MainWindow::Impl`**

Add the include `#include "slideio/viewer/app/WorkspaceSwitch.h"` and `#include "slideio/viewer/ui/WorkspaceChangeDialog.h"` beside the other app and ui includes, then add the member beside `annotationPersistence`:

```cpp
    std::unique_ptr<app::WorkspaceSwitch> workspaceSwitch;
```

Immediately after `annotationPersistence` is constructed and its resolvers are set, build it:

```cpp
    workspaceSwitch = std::make_unique<app::WorkspaceSwitch>(
        *annotationRepository, *annotations, *annotationPersistence,
        [this]() { return annotationRepository->workspaceRoot(); },
        [this](std::string root) { annotationRepository->setWorkspaceRoot(std::move(root)); });
    workspaceSwitch->setResolver(
        [this](const core::WorkspaceSwitchFacts& facts, const core::WorkspaceSwitchOptions& options) {
            WorkspaceChangeDialog dialog(facts, options, owner);
            dialog.exec();
            return dialog.choice();
        });
```

- [ ] **Step 3: Replace the body of the "folder changed" branch in `openPreferences()`**

**`openPreferences()` must stop calling `endSlide()` itself.** `WorkspaceSwitch::run`
calls it, and `facts.currentWritable` is read from `isActive()` *before* that
call. A flush already performed by the caller would leave the service inactive,
every switch would look read-only, and copy, keep-current and merge would be
silently withheld for the rest of the program's life. One flush, one owner.

Detecting the save-failure rescue moves with the flush: `run()` compares the
root before and after, which is the same signal the old code got from comparing
the settings value.

Replace the whole body of `if (requested != QString::fromStdString(annotationRepository->workspaceRoot())) { ... }` — everything from the `commitPendingEdit()` call to the closing brace, including the `beforeFlush`/`afterFlush` block and the trailing `reopenCurrentScene()` — with:

```cpp
        // Before everything: a field typed into but not yet committed would
        // otherwise be lost, and this is the one close path that does not go
        // through closeSlide()'s aboutToCloseSlide listener.
        if (annotationPropertiesPanel != nullptr) {
            annotationPropertiesPanel->commitPendingEdit();
        }

        // Captured before the setting is written, so Cancel can put it back.
        const QString previousWorkspace = annotationSettings->workspaceDirectory();

        annotationSettings->setWorkspaceDirectory(entered);
        // Read the path back rather than reusing `entered`: an empty or
        // whitespace-only entry clears the setting, and the repository must
        // then follow the default workspace rather than root itself at "".
        const QString resolved = annotationSettings->workspaceDirectory();

        if (!viewportWidget->isSlideOpen()) {
            // Nothing to carry over and nothing to ask about.
            annotationRepository->setWorkspaceRoot(resolved.toStdString());
            return annotationSettings->hasUserName();
        }

        // WorkspaceSwitch re-points the repository itself, because what it
        // writes and where depends on what the new folder turns out to hold. It
        // also re-enters the slide, which is why there is no
        // reopenCurrentScene() here any more: a workspace change affects no
        // pixel, and routing it through closeSlide() is what let a pending edit
        // arrive after the flush.
        core::SlideProvenance provenance;
        const core::SlideInfo& info = viewportWidget->slideInfo();
        provenance.path = viewportWidget->currentFilePath();
        provenance.fileName =
            QFileInfo(QString::fromStdString(provenance.path)).fileName().toStdString();
        provenance.width = info.width;
        provenance.height = info.height;

        const core::AnnotationKey key{viewportWidget->currentSlideId(),
                                      viewportWidget->currentSceneIndex()};
        const app::WorkspaceSwitchResult switched =
            workspaceSwitch->run(resolved.toStdString(), key, provenance);

        if (switched.rescued) {
            // The flush failed and the save-failure resolver rescued it to a
            // folder the user picked mid-failure, which it has already made the
            // workspace. Applying the dialog's value on top would strand the
            // file it just saved somewhere this application never looks again,
            // and say nothing about it. The more recent choice wins; the user is
            // told where their work went. Gated on the flush actually having
            // succeeded: when the retries failed too, endSlide() abandoned the
            // annotations and claiming they were saved would be false.
            const QString folder =
                QDir::toNativeSeparators(annotationSettings->workspaceDirectory());
            if (switched.flush == core::SaveStatus::Saved) {
                statusBarManager->showTransientMessage(
                    tr("Annotations were saved to %1, which is now the annotation folder "
                       "(your entry was not used).")
                        .arg(folder));
            } else {
                statusBarManager->showTransientMessage(
                    tr("The annotation folder is now %1 (your entry was not used). "
                       "The annotations were NOT saved.")
                        .arg(folder));
            }
            return annotationSettings->hasUserName();
        }

        if (switched.choice == app::WorkspaceChoice::Cancel) {
            // The folder in use did not change, so neither should the setting.
            annotationSettings->setWorkspaceDirectory(previousWorkspace);
        }
        reportWorkspaceSwitch(switched);
```

Note on the undo stack. The spec requires it cleared on every switch, and no
new code does that: `run()`'s closing `beginSlide` calls `replaceAll`, which
emits `modelReset`, which `MainWindow` already answers with
`clearUndoUnlessReopening` (`MainWindow.cpp:1225`). That guard only declines
while `ViewportWidget::isPreservingUndoContext()` is true, and
`installSceneOpenResult` sets the flag back to false when an open finishes
(`ViewportWidget.cpp:2629`) — so it is false whenever Preferences can be
reached. Do not add a second clear; verify this one fires with check X11 in the
spec's group X list.

- [ ] **Step 4: Add the status-bar receipt**

Add this private method to `MainWindow::Impl`, beside the other reporting helpers:

```cpp
void MainWindow::Impl::reportWorkspaceSwitch(const app::WorkspaceSwitchResult& result)
{
    // A merge that silently changes a set of clinical findings is not
    // acceptable; the count is the receipt.
    if (result.write == core::SaveStatus::NotWritable || result.write == core::SaveStatus::Failed) {
        statusBarManager->showTransientMessage(
            tr("The annotations could not be written to the new folder (%1). They are still in the "
               "folder you switched from.")
                .arg(QDir::toNativeSeparators(QString::fromStdString(result.writePath))));
        return;
    }

    switch (result.choice) {
    case app::WorkspaceChoice::Merge:
        statusBarManager->showTransientMessage(tr("Merged: %1 added, %2 superseded, %3 kept.")
                                                   .arg(result.merge.adopted)
                                                   .arg(result.merge.superseded)
                                                   .arg(result.merge.kept));
        break;
    case app::WorkspaceChoice::Copy:
    case app::WorkspaceChoice::KeepCurrent:
        statusBarManager->showTransientMessage(
            tr("Annotations copied to the new folder."));
        break;
    case app::WorkspaceChoice::LoadNew:
        if (result.asked) {
            statusBarManager->showTransientMessage(
                tr("Now showing the new folder's annotations. The previous ones are unchanged where "
                   "they were."));
        }
        break;
    case app::WorkspaceChoice::Cancel:
        statusBarManager->showTransientMessage(tr("The annotation folder was not changed."));
        break;
    }
}
```

Declare it in the `Impl` class beside the other helpers:

```cpp
    void reportWorkspaceSwitch(const app::WorkspaceSwitchResult& result);
```

- [ ] **Step 5: Build and run every suite**

Run: `cmake --build build/build --config Release` then `ctest --test-dir build/build -C Release --output-on-failure`
Expected: builds clean; 4/4 suites pass.

- [ ] **Step 6: Install and verify the artifact**

Close any running viewer first — a running viewer turns `cmake --install` into a silent no-op that still exits 0.

Run: `cmake --install build/build --config Release`
Then verify the installed binary is the one just built, by hash rather than by exit code:

```bash
sha256sum build/build/Release/slideio-viewer.exe build/install/release/bin/slideio-viewer.exe
```

Expected: the two hashes match.

- [ ] **Step 7: Commit**

```bash
git add src/ui/src/MainWindow.cpp src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp
git commit -m "Ask what should happen to annotations when the workspace changes"
```

- [ ] **Step 8: Hand the group X checks to the project owner**

The dialog and the wiring are not reachable from any suite. Report that group X (ten checks, in the spec) is outstanding, and name X5 and X9 as the two worth running first: X5 (cancel, then confirm a later edit still autosaves) is the only check that catches a service left deactivated, and X9 (no tile flicker during a switch) is the only evidence the switch stopped going through `closeSlide()`.
