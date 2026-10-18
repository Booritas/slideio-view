# Categorized Counting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pathologist picks a counting category once and then clicks: every point marker carries that category, the per-category tallies are visible while counting, and a mistyped category can be renamed so every annotation carrying it is retagged in one undo step.

**Architecture:** The category is the existing `core::AnnotationProperties::classification` string, so the file format is untouched. Two pure functions go in core (`normalizeCategoryList`, `retagClassification`), the user's list goes in an infra QSettings store, the stamping rule goes in `app::AnnotationModel::add()`, the bulk undo step goes in a second `ICommand`, and the tally goes in `ui::AnnotationListPolicy`. Every rule that can be tested is placed where a test can reach it; what remains is panel wiring, covered by manual group AC.

**Tech Stack:** C++17, Qt 6.7.3 Widgets, CMake 3.20+/Conan 2, Catch2 v3, four ctest suites (core, app, infra, ui).

**Spec:** `docs/superpowers/specs/2026-10-18-annotation-categorized-counting-design.md` (committed `714aee5`)

## Global Constraints

- Layering is strictly downward: core (Qt-free, links no Qt target) → app (core + Qt6::Core) → infra → ui. Core must never name anything in app, infra or ui.
- **No test binary creates a `QApplication`, so no test may construct a widget.** Anything in a panel, dialog or `ViewportWidget` is unreachable by every suite.
- `schemaVersion` stays **1**. `src/core/src/AnnotationSerialization.cpp` is **not modified by this plan at all** — `classification` is already written and read.
- Defaults when the category list is unset, in this order: `Tumor`, `Stroma`, `Necrosis`, `Normal`, `Artifact`.
- "Unclassified" is never a configured category. It is the absence of a value.
- Category matching is case-insensitive everywhere (ASCII folding, as `ui::toLower` already does).
- Build: `cmake --build build/build --config Release`. Test: `ctest --test-dir build/build -C Release --output-on-failure`.
- `core-tests` is the only suite safe to run as a bare `.exe` (`build/build/tests/Release/slideio-viewer-core-tests.exe`). **Never** run `app-tests`, `infra-tests` or `ui-tests` directly — they resolve their DLLs through a ctest `ENVIRONMENT_MODIFICATION` property, and a bare run pops a modal Windows error box that blocks until a human dismisses it.
- Every `.cpp` and every test file is listed explicitly in CMake. CMake's generate step requires every listed source to **exist**, so create the file before adding it to a source list and re-running `cmake -S . -B build/build`.
- Commits go directly to `main`. No PRs, no feature branches.
- A transient `MSB6003` tlog file-lock error during a build is not a code error; retry the build once before investigating.

## Review Focus

The spec implies these and no task's tests covered them until this list was written. Each now has a test, in the task named.

1. **Rename to the same name must record no undo step.** A user opens Rename, changes nothing, presses OK; a command that records nothing is "worse than no undo, because they will assume it worked" (`AnnotationEditCommand.h`). Pinned in Task 2.
2. **A rename target with surrounding whitespace must be trimmed.** Otherwise `" Mitosis "` becomes a category that can never match the configured list and shows as its own tally row. Pinned in Task 2.
3. **A whitespace-only classification counts as Unclassified.** Annotation files are hand-editable; a row labelled with three spaces is unreadable and unselectable. Pinned in Task 6.
4. **`clear()` on a slide switch must not reset the current category.** Spec §4.4 says it survives a slide switch, and `clear()` is called on every slide change. Pinned in Task 4.
5. **A retag of many annotations must emit exactly one model reset.** `AnnotationListPanel` rebuilds its whole tree on every `annotationChanged`, so N restores would be N full rebuilds. Pinned in Task 5.

## File Structure

| File | Responsibility |
|---|---|
| `src/core/include/slideio/viewer/core/AnnotationCategories.h` (new) | `normalizeCategoryList`, `RetagChange`, `retagClassification` — the two pure rules about category data |
| `src/core/src/AnnotationCategories.cpp` (new) | Their implementation |
| `src/infra/include/slideio/viewer/infra/QSettingsAnnotationCategoryStore.h` (new) | The user's list, persisted; INI-path ctor for tests |
| `src/infra/src/QSettingsAnnotationCategoryStore.cpp` (new) | Its implementation |
| `src/app/include/slideio/viewer/app/AnnotationRetagCommand.h` (new) | One undo step covering many annotations |
| `src/app/src/AnnotationRetagCommand.cpp` (new) | Its implementation |
| `src/app/include/slideio/viewer/app/AnnotationModel.h` / `src/app/src/AnnotationModel.cpp` | Gains `setCurrentCategory`, the stamping rule in `add()`, and `restoreMany` |
| `src/ui/include/slideio/viewer/ui/AnnotationListPolicy.h` / `src/ui/src/AnnotationListPolicy.cpp` | Gains `CategoryTallyRow` and `categoryTally` |
| `src/ui/include/slideio/viewer/ui/AnnotationPropertiesEdit.h` | `kAnnotationClassifications` becomes the default seed only |
| `src/ui/src/AnnotationPropertiesPanel.cpp` | Its combo reads the configured list instead of the array |
| `src/ui/include/slideio/viewer/ui/PreferencesDialog.h` / `src/ui/src/PreferencesDialog.cpp` | A third row editing the list |
| `src/ui/src/AnnotationListPanel.cpp` | The counting strip: combo, tally rows, context menu |
| `src/ui/src/MainWindow.cpp` | Owns the store, feeds both panels, gates the combo, applies join-on-first-use, runs rename/remove |
| `CLAUDE.md` | The "one command type" claim is corrected; the new decisions recorded |

## Plan-level rulings

Two decisions the spec does not make, taken here with their reasons, so an implementer does not have to re-derive them:

1. **`AnnotationModel` gains `restoreMany`, emitting one `modelReset`.** The spec says the retag is one undo step; it does not say how. `restore()` emits `annotationChanged`, and `AnnotationListPanel` rebuilds its entire tree on that signal (`AnnotationListPanel.cpp:112`), so retagging 500 markers through 500 `restore()` calls would rebuild a 500-row tree 500 times. *Cost if wrong:* one extra model method that duplicates `restore` in a loop.
2. **`retagClassification` builds its `after` state with `Annotation`'s four-argument constructor, never `setProperties`.** `Annotation::setProperties` reads the system clock and restamps `modifiedAt` (`Annotation.cpp:57-61`), which would make the function impure and make a redo produce a different timestamp from the original action. The four-argument constructor is documented as restoring "exactly as stored, including its timestamps". *Cost if wrong:* undo and redo stop being exact inverses and autosave writes a modification time nobody made.

---

### Task 1: core — the category list normaliser

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationCategories.h`
- Create: `src/core/src/AnnotationCategories.cpp`
- Create: `tests/core/AnnotationCategoriesTest.cpp`
- Modify: `src/core/CMakeLists.txt:18` (after `src/AnnotationDecoration.cpp`)
- Modify: `tests/CMakeLists.txt:20` (after `core/AnnotationDecorationTest.cpp`)

**Interfaces:**
- Consumes: nothing.
- Produces: `std::vector<std::string> core::normalizeCategoryList(const std::vector<std::string>& raw)`.

- [ ] **Step 1: Create the two files with an empty namespace**

CMake's generate step fails on a listed source that does not exist, so the files must exist before Step 3.

`src/core/include/slideio/viewer/core/AnnotationCategories.h`:

```cpp
#pragma once

#include <string>
#include <vector>

namespace slideio::viewer::core
{

} // namespace slideio::viewer::core
```

`src/core/src/AnnotationCategories.cpp`:

```cpp
#include "slideio/viewer/core/AnnotationCategories.h"

namespace slideio::viewer::core
{

} // namespace slideio::viewer::core
```

`tests/core/AnnotationCategoriesTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationCategories.h"
```

- [ ] **Step 2: Register all three in CMake**

In `src/core/CMakeLists.txt`, add after line 18 (`    src/AnnotationDecoration.cpp`):

```cmake
    src/AnnotationCategories.cpp
```

In `tests/CMakeLists.txt`, add after line 20 (`    core/AnnotationDecorationTest.cpp`):

```cmake
    core/AnnotationCategoriesTest.cpp
```

- [ ] **Step 3: Regenerate the build system**

Run: `cmake -S . -B build/build`
Expected: configures and generates without error. A "Cannot find source file" error here means Step 1 was skipped.

- [ ] **Step 4: Write the failing tests**

Append to `tests/core/AnnotationCategoriesTest.cpp`:

```cpp
using slideio::viewer::core::normalizeCategoryList;

TEST_CASE("normalizeCategoryList trims and drops blanks", "[core][Categories]")
{
    const std::vector<std::string> out =
        normalizeCategoryList({"  Tumor  ", "", "   ", "Stroma"});

    REQUIRE(out == std::vector<std::string>{"Tumor", "Stroma"});
}

TEST_CASE("normalizeCategoryList drops case-insensitive duplicates keeping the first spelling",
          "[core][Categories]")
{
    const std::vector<std::string> out =
        normalizeCategoryList({"Mitosis", "MITOSIS", "mitosis"});

    REQUIRE(out == std::vector<std::string>{"Mitosis"});
}

TEST_CASE("normalizeCategoryList preserves the order given", "[core][Categories]")
{
    // Never sorted: categoryTally displays in list order, so a list that
    // sorted itself would reorder the counting rows behind the user.
    const std::vector<std::string> out =
        normalizeCategoryList({"Zeta", "Alpha", "Mitosis"});

    REQUIRE(out == std::vector<std::string>{"Zeta", "Alpha", "Mitosis"});
}

TEST_CASE("normalizeCategoryList on an empty or all-blank list gives an empty list",
          "[core][Categories]")
{
    REQUIRE(normalizeCategoryList({}).empty());
    REQUIRE(normalizeCategoryList({"", "  ", "\t"}).empty());
}
```

- [ ] **Step 5: Run the tests to watch them fail**

Run: `cmake --build build/build --config Release && build/build/tests/Release/slideio-viewer-core-tests.exe "[Categories]"`
Expected: the build FAILS with an error naming `normalizeCategoryList` as undeclared.

- [ ] **Step 6: Declare it in the header**

Insert inside the namespace in `AnnotationCategories.h`:

```cpp
/// The user's category list, cleaned up for storage and display.
///
/// Trims each entry, drops entries that are empty once trimmed, and removes
/// case-insensitive duplicates keeping the FIRST spelling seen. Order is
/// otherwise preserved and is never sorted: categoryTally() displays tallies in
/// list order, and a list that sorted itself would reorder the counting rows
/// behind the user mid-count.
[[nodiscard]] std::vector<std::string> normalizeCategoryList(
    const std::vector<std::string>& raw);
```

- [ ] **Step 7: Implement it**

In `AnnotationCategories.cpp`, above the declaration's definition:

```cpp
#include <algorithm>
#include <cctype>

namespace slideio::viewer::core
{
namespace
{

std::string toLowerAscii(const std::string& text)
{
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

std::string trimmed(const std::string& text)
{
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto first = std::find_if(text.begin(), text.end(), notSpace);
    const auto last = std::find_if(text.rbegin(), text.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}

} // namespace

std::vector<std::string> normalizeCategoryList(const std::vector<std::string>& raw)
{
    std::vector<std::string> result;
    std::vector<std::string> seen;
    for (const std::string& entry : raw) {
        const std::string clean = trimmed(entry);
        if (clean.empty()) {
            continue;
        }
        const std::string key = toLowerAscii(clean);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        result.push_back(clean);
    }
    return result;
}

} // namespace slideio::viewer::core
```

- [ ] **Step 8: Run the tests to watch them pass**

Run: `cmake --build build/build --config Release && build/build/tests/Release/slideio-viewer-core-tests.exe "[Categories]"`
Expected: PASS, 4 test cases.

- [ ] **Step 9: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

- [ ] **Step 10: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationCategories.h src/core/src/AnnotationCategories.cpp tests/core/AnnotationCategoriesTest.cpp src/core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add the category list normaliser"
```

---

### Task 2: core — retagging a classification

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationCategories.h`
- Modify: `src/core/src/AnnotationCategories.cpp`
- Test: `tests/core/AnnotationCategoriesTest.cpp`

**Interfaces:**
- Consumes: `normalizeCategoryList` (same header; not called, just co-located).
- Produces: `struct core::RetagChange { std::string id; Annotation before; Annotation after; };` and `std::vector<RetagChange> core::retagClassification(const std::vector<Annotation>& annotations, const std::string& from, const std::string& to, std::chrono::system_clock::time_point modifiedAt)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/AnnotationCategoriesTest.cpp`:

```cpp
#include "slideio/viewer/core/Annotation.h"

#include <chrono>

using slideio::viewer::core::Annotation;
using slideio::viewer::core::AnnotationProperties;
using slideio::viewer::core::PointF;
using slideio::viewer::core::PointGeometry;
using slideio::viewer::core::retagClassification;

namespace
{

Annotation markerWith(const std::string& id, const std::string& classification)
{
    Annotation annotation(id, PointGeometry{PointF{1.0, 2.0}});
    AnnotationProperties properties = annotation.properties();
    properties.classification = classification;
    annotation.setProperties(std::move(properties));
    return annotation;
}

constexpr std::chrono::system_clock::time_point kStamp{std::chrono::seconds{1'700'000'000}};

} // namespace

TEST_CASE("retagClassification matches case-insensitively and rewrites the classification",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "mitosis"),
                                              markerWith("b", "MITOSIS")};

    const auto changes = retagClassification(annotations, "Mitosis", "Mitotic figure", kStamp);

    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].id == "a");
    REQUIRE(changes[0].before.properties().classification == "mitosis");
    REQUIRE(changes[0].after.properties().classification == "Mitotic figure");
    REQUIRE(changes[1].after.properties().classification == "Mitotic figure");
}

TEST_CASE("retagClassification leaves non-matching annotations out of the result entirely",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis"),
                                              markerWith("b", "Lymphocyte")};

    const auto changes = retagClassification(annotations, "Mitosis", "Mitotic figure", kStamp);

    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].id == "a");
}

TEST_CASE("retagClassification stamps modifiedAt from its parameter, not the clock",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis")};

    const auto changes = retagClassification(annotations, "Mitosis", "Other", kStamp);

    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].after.metadata().modifiedAt == kStamp);
    // The before state keeps whatever it had, so undo restores it verbatim.
    REQUIRE(changes[0].before.metadata().modifiedAt
            == annotations[0].metadata().modifiedAt);
    REQUIRE(changes[0].after.metadata().createdAt == annotations[0].metadata().createdAt);
}

TEST_CASE("retagClassification renaming a category to itself changes nothing",
          "[core][Categories]")
{
    // Review Focus 1: the user opens Rename, edits nothing, presses OK. An
    // empty result means the caller records no undo step, and a step that does
    // nothing is worse than no step because the user assumes it worked.
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis")};

    REQUIRE(retagClassification(annotations, "Mitosis", "Mitosis", kStamp).empty());
    REQUIRE(retagClassification(annotations, "Mitosis", "mitosis", kStamp).empty());
}

TEST_CASE("retagClassification trims its target", "[core][Categories]")
{
    // Review Focus 2: an untrimmed target can never match the configured list,
    // so it would show as its own tally row forever.
    const std::vector<Annotation> annotations{markerWith("a", "Mitosis")};

    const auto changes = retagClassification(annotations, "Mitosis", "  Mitotic figure  ", kStamp);

    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].after.properties().classification == "Mitotic figure");
}

TEST_CASE("retagClassification finds nothing when no annotation carries the category",
          "[core][Categories]")
{
    const std::vector<Annotation> annotations{markerWith("a", "Lymphocyte")};

    REQUIRE(retagClassification(annotations, "Mitosis", "Other", kStamp).empty());
}
```

- [ ] **Step 2: Run the tests to watch them fail**

Run: `cmake --build build/build --config Release && build/build/tests/Release/slideio-viewer-core-tests.exe "[Categories]"`
Expected: the build FAILS with an error naming `retagClassification` as undeclared.

- [ ] **Step 3: Declare the type and function**

Add to `AnnotationCategories.h` — note the new includes:

```cpp
#include "slideio/viewer/core/Annotation.h"

#include <chrono>
```

and inside the namespace:

```cpp
/// One annotation's exact before and after state for a retag.
struct RetagChange
{
    std::string id;
    Annotation before;
    Annotation after;
};

/// Every annotation whose classification equals `from` (case-insensitively),
/// paired with the state it should take.
///
/// `to` is trimmed; renaming a category to itself, or to the same word in a
/// different case, yields an EMPTY result, and an empty result means the caller
/// records no undo step at all -- the rule AnnotationEditCommand::create already
/// applies when it refuses to build a command that records nothing.
///
/// `modifiedAt` is a parameter rather than a clock read so this stays pure and
/// testable, as core::shouldAutosave is, and so `after` is the EXACT final
/// state. A redo that re-read the clock would produce a different timestamp
/// from the original action, and undo/redo would stop being exact inverses.
[[nodiscard]] std::vector<RetagChange> retagClassification(
    const std::vector<Annotation>& annotations, const std::string& from, const std::string& to,
    std::chrono::system_clock::time_point modifiedAt);
```

- [ ] **Step 4: Implement it**

Append inside the namespace in `AnnotationCategories.cpp`:

```cpp
std::vector<RetagChange> retagClassification(const std::vector<Annotation>& annotations,
                                             const std::string& from, const std::string& to,
                                             std::chrono::system_clock::time_point modifiedAt)
{
    const std::string target = trimmed(to);
    const std::string fromKey = toLowerAscii(trimmed(from));
    if (fromKey.empty() || fromKey == toLowerAscii(target)) {
        // Nothing to do, and nothing to record. Renaming a category to itself
        // must not leave a step on the undo stack that does nothing.
        return {};
    }

    std::vector<RetagChange> changes;
    for (const Annotation& source : annotations) {
        if (toLowerAscii(trimmed(source.properties().classification)) != fromKey) {
            continue;
        }
        AnnotationProperties properties = source.properties();
        properties.classification = target;
        AnnotationMetadata metadata = source.metadata();
        metadata.modifiedAt = modifiedAt;
        // The four-argument constructor, never setProperties(): that one reads
        // the system clock and restamps modifiedAt, which would make this
        // function impure and break the exactness `after` promises.
        changes.push_back(RetagChange{source.id(), source,
                                      Annotation{source.id(), source.geometry(),
                                                 std::move(properties), metadata}});
    }
    return changes;
}
```

- [ ] **Step 5: Run the tests to watch them pass**

Run: `cmake --build build/build --config Release && build/build/tests/Release/slideio-viewer-core-tests.exe "[Categories]"`
Expected: PASS, 10 test cases.

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationCategories.h src/core/src/AnnotationCategories.cpp tests/core/AnnotationCategoriesTest.cpp
git commit -m "Retag every annotation carrying one classification"
```

---

### Task 3: infra — the category list store

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/QSettingsAnnotationCategoryStore.h`
- Create: `src/infra/src/QSettingsAnnotationCategoryStore.cpp`
- Create: `tests/infra/QSettingsAnnotationCategoryStoreTest.cpp`
- Modify: `src/infra/CMakeLists.txt:6` (after `src/QSettingsColorProfileOverrideStore.cpp`)
- Modify: `tests/CMakeLists.txt:56` (after `infra/QSettingsColorProfileOverrideStoreTest.cpp`)

**Interfaces:**
- Consumes: `core::normalizeCategoryList` from Task 1.
- Produces: `infra::QSettingsAnnotationCategoryStore` with `std::vector<std::string> categories() const` and `void setCategories(const std::vector<std::string>&)`, plus `static std::vector<std::string> defaultCategories()`.

- [ ] **Step 1: Create the files with an empty namespace**

Mirror `QSettingsColorProfileOverrideStore`, which this follows exactly.

`src/infra/include/slideio/viewer/infra/QSettingsAnnotationCategoryStore.h`:

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>

class QSettings;

namespace slideio::viewer::infra
{

} // namespace slideio::viewer::infra
```

`src/infra/src/QSettingsAnnotationCategoryStore.cpp`:

```cpp
#include "slideio/viewer/infra/QSettingsAnnotationCategoryStore.h"

namespace slideio::viewer::infra
{

} // namespace slideio::viewer::infra
```

`tests/infra/QSettingsAnnotationCategoryStoreTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/QSettingsAnnotationCategoryStore.h"
```

- [ ] **Step 2: Register all three in CMake**

In `src/infra/CMakeLists.txt`, after line 6:

```cmake
    src/QSettingsAnnotationCategoryStore.cpp
```

In `tests/CMakeLists.txt`, after line 56:

```cmake
    infra/QSettingsAnnotationCategoryStoreTest.cpp
```

This class is **not** a `QObject` — it holds a `QSettings`, it is not one — so it must not be added to any explicit header list for AUTOMOC.

- [ ] **Step 3: Regenerate the build system**

Run: `cmake -S . -B build/build`
Expected: configures and generates without error.

- [ ] **Step 4: Write the failing tests**

Append to `tests/infra/QSettingsAnnotationCategoryStoreTest.cpp`:

```cpp
#include <QTemporaryDir>

#include <string>
#include <vector>

using slideio::viewer::infra::QSettingsAnnotationCategoryStore;

TEST_CASE("An unset category store answers the defaults", "[infra][CategoryStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    const QSettingsAnnotationCategoryStore store(ini);

    REQUIRE(store.categories()
            == std::vector<std::string>{"Tumor", "Stroma", "Necrosis", "Normal", "Artifact"});
}

TEST_CASE("A stored category list survives a new store over the same file",
          "[infra][CategoryStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    {
        QSettingsAnnotationCategoryStore store(ini);
        store.setCategories({"Mitosis", "Lymphocyte"});
    }

    // A second store over the same file: this is the across-sessions claim.
    const QSettingsAnnotationCategoryStore reopened(ini);

    REQUIRE(reopened.categories() == std::vector<std::string>{"Mitosis", "Lymphocyte"});
}

TEST_CASE("The store normalises what it is given", "[infra][CategoryStore]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    QSettingsAnnotationCategoryStore store(ini);
    store.setCategories({"  Mitosis  ", "", "MITOSIS", "Lymphocyte"});

    REQUIRE(store.categories() == std::vector<std::string>{"Mitosis", "Lymphocyte"});
}

TEST_CASE("An explicitly empty category list is kept, not replaced by the defaults",
          "[infra][CategoryStore]")
{
    // A user who clears the list in Preferences meant to clear it. Answering
    // the defaults here would resurrect five categories they deleted.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string ini = dir.filePath("categories.ini").toStdString();

    {
        QSettingsAnnotationCategoryStore store(ini);
        store.setCategories({});
    }

    const QSettingsAnnotationCategoryStore reopened(ini);

    REQUIRE(reopened.categories().empty());
}
```

- [ ] **Step 5: Run the tests to watch them fail**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R infra-tests --output-on-failure`
Expected: the build FAILS with an error naming `QSettingsAnnotationCategoryStore` as undefined.

- [ ] **Step 6: Declare the class**

Inside the namespace in `QSettingsAnnotationCategoryStore.h`:

```cpp
/// The user's annotation category list, persisted in QSettings under
/// "annotations/categories".
///
/// This deliberately has NO core interface, unlike
/// QSettingsColorProfileOverrideStore. That one exists because core reaches it.
/// Nothing in core needs the category list -- core needs only the pure
/// normalizeCategoryList -- and the consumers are ui panels, which already link
/// infra. An interface here would be inversion with nothing to invert.
class QSettingsAnnotationCategoryStore
{
public:
    /// FR-ANN-13's defaults, answered when nothing has ever been stored.
    /// "Unclassified" is not among them: it is the absence of a value.
    [[nodiscard]] static std::vector<std::string> defaultCategories();

    /// The application's own settings, as QSettings resolves them from the
    /// organisation and application names.
    QSettingsAnnotationCategoryStore();

    /// An explicit INI file. Tests use this so they never touch the user's
    /// real configuration.
    explicit QSettingsAnnotationCategoryStore(const std::string& iniFilePath);

    ~QSettingsAnnotationCategoryStore();

    /// The stored list, or the defaults when nothing has ever been stored. An
    /// explicitly emptied list is returned empty: clearing it was a choice.
    [[nodiscard]] std::vector<std::string> categories() const;

    /// Stores `categories` after core::normalizeCategoryList.
    void setCategories(const std::vector<std::string>& categories);

private:
    std::unique_ptr<QSettings> m_settings;
};
```

- [ ] **Step 7: Implement it**

`QSettingsAnnotationCategoryStore.cpp`:

```cpp
#include "slideio/viewer/infra/QSettingsAnnotationCategoryStore.h"

#include "slideio/viewer/core/AnnotationCategories.h"

#include <QSettings>
#include <QString>
#include <QStringList>

namespace slideio::viewer::infra
{
namespace
{
constexpr const char* kCategoriesKey = "annotations/categories";
}

std::vector<std::string> QSettingsAnnotationCategoryStore::defaultCategories()
{
    return {"Tumor", "Stroma", "Necrosis", "Normal", "Artifact"};
}

QSettingsAnnotationCategoryStore::QSettingsAnnotationCategoryStore()
    : m_settings(std::make_unique<QSettings>())
{
}

QSettingsAnnotationCategoryStore::QSettingsAnnotationCategoryStore(const std::string& iniFilePath)
    : m_settings(std::make_unique<QSettings>(QString::fromStdString(iniFilePath),
                                             QSettings::IniFormat))
{
}

QSettingsAnnotationCategoryStore::~QSettingsAnnotationCategoryStore() = default;

std::vector<std::string> QSettingsAnnotationCategoryStore::categories() const
{
    if (!m_settings->contains(kCategoriesKey)) {
        return defaultCategories();
    }
    const QStringList stored = m_settings->value(kCategoriesKey).toStringList();
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(stored.size()));
    for (const QString& entry : stored) {
        result.push_back(entry.toStdString());
    }
    return result;
}

void QSettingsAnnotationCategoryStore::setCategories(const std::vector<std::string>& categories)
{
    QStringList stored;
    for (const std::string& entry : core::normalizeCategoryList(categories)) {
        stored.append(QString::fromStdString(entry));
    }
    m_settings->setValue(kCategoriesKey, stored);
    m_settings->sync();
}

} // namespace slideio::viewer::infra
```

- [ ] **Step 8: Run the tests to watch them pass**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R infra-tests --output-on-failure`
Expected: PASS.

- [ ] **Step 9: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

- [ ] **Step 10: Commit**

```bash
git add src/infra/include/slideio/viewer/infra/QSettingsAnnotationCategoryStore.h src/infra/src/QSettingsAnnotationCategoryStore.cpp tests/infra/QSettingsAnnotationCategoryStoreTest.cpp src/infra/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Persist the user's annotation category list"
```

---

### Task 4: app — the current category, stamped on markers

**Files:**
- Modify: `src/app/include/slideio/viewer/app/AnnotationModel.h` (beside `setDefaultAuthor`, around line 67)
- Modify: `src/app/src/AnnotationModel.cpp:30-42` (`add`) and `:101-104` (beside `setDefaultAuthor`)
- Test: `tests/app/AnnotationModelTest.cpp`

**Interfaces:**
- Consumes: `core::typeOf(const AnnotationGeometry&)` from `core/AnnotationGeometry.h`.
- Produces: `void app::AnnotationModel::setCurrentCategory(std::string)` and `const std::string& app::AnnotationModel::currentCategory() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/app/AnnotationModelTest.cpp` (match the file's existing `using` style rather than adding qualifications it does not use):

```cpp
TEST_CASE("The current category is stamped onto a new point marker", "[app][AnnotationModel]")
{
    AnnotationModel model;
    model.setCurrentCategory("Mitosis");

    const std::string id = model.add(core::PointGeometry{core::PointF{1.0, 2.0}});

    REQUIRE(model.find(id) != nullptr);
    REQUIRE(model.find(id)->properties().classification == "Mitosis");
}

TEST_CASE("The current category is not stamped onto other geometry types",
          "[app][AnnotationModel]")
{
    // Deliberate: a rectangle drawn for an unrelated reason must not acquire a
    // clinical classification because the user was counting ten minutes ago.
    AnnotationModel model;
    model.setCurrentCategory("Mitosis");

    const std::string box = model.add(core::RectangleGeometry{core::RectF{0.0, 0.0, 4.0, 4.0}});
    const std::string arrow =
        model.add(core::ArrowGeometry{core::PointF{0.0, 0.0}, core::PointF{3.0, 4.0}});

    REQUIRE(model.find(box)->properties().classification.empty());
    REQUIRE(model.find(arrow)->properties().classification.empty());
}

TEST_CASE("An empty current category leaves a marker unclassified", "[app][AnnotationModel]")
{
    AnnotationModel model;

    const std::string id = model.add(core::PointGeometry{core::PointF{1.0, 2.0}});

    REQUIRE(model.find(id)->properties().classification.empty());
}

TEST_CASE("Changing the current category does not touch existing annotations",
          "[app][AnnotationModel]")
{
    AnnotationModel model;
    model.setCurrentCategory("Mitosis");
    const std::string first = model.add(core::PointGeometry{core::PointF{1.0, 2.0}});

    model.setCurrentCategory("Lymphocyte");

    REQUIRE(model.find(first)->properties().classification == "Mitosis");
}

TEST_CASE("Clearing the model for a new slide keeps the current category",
          "[app][AnnotationModel]")
{
    // Review Focus 4: spec 4.4 says the current category survives a slide
    // switch, and clear() is called on every slide change.
    AnnotationModel model;
    model.setCurrentCategory("Mitosis");

    model.clear();
    const std::string id = model.add(core::PointGeometry{core::PointF{1.0, 2.0}});

    REQUIRE(model.currentCategory() == "Mitosis");
    REQUIRE(model.find(id)->properties().classification == "Mitosis");
}
```

- [ ] **Step 2: Run the tests to watch them fail**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R app-tests --output-on-failure`
Expected: the build FAILS with an error naming `setCurrentCategory` as not a member of `AnnotationModel`.

- [ ] **Step 3: Declare the two methods**

In `AnnotationModel.h`, immediately after the `setDefaultAuthor` declaration:

```cpp
    /// Stamped into the classification of POINT markers created through add(),
    /// and of nothing else. The test is on the geometry rather than on the
    /// active tool because the model does not know about tools and should not
    /// learn: the rule then holds for every path that creates a marker.
    ///
    /// Creation only. Changing this never alters an annotation that already
    /// exists; retagging what exists is AnnotationRetagCommand, which is
    /// explicit and undoable.
    ///
    /// Session state: it deliberately survives clear(), because a study is
    /// often several slides counted the same way.
    void setCurrentCategory(std::string category);
    [[nodiscard]] const std::string& currentCategory() const;
```

- [ ] **Step 4: Implement them and the stamping rule**

In `AnnotationModel.cpp`, beside `setDefaultAuthor`:

```cpp
void AnnotationModel::setCurrentCategory(std::string category)
{
    m_currentCategory = std::move(category);
}

const std::string& AnnotationModel::currentCategory() const
{
    return m_currentCategory;
}
```

Add the member beside `m_defaultAuthor` in the header's private section:

```cpp
    std::string m_currentCategory;
```

And in `add()`, replace the `core::AnnotationProperties{}` argument:

```cpp
std::string AnnotationModel::add(core::AnnotationGeometry geometry)
{
    std::string id = generateId();
    core::AnnotationMetadata metadata;
    metadata.author = m_defaultAuthor;
    const auto now = std::chrono::system_clock::now();
    metadata.createdAt = now;
    metadata.modifiedAt = now;

    core::AnnotationProperties properties;
    // Markers only: see setCurrentCategory.
    if (core::typeOf(geometry) == core::AnnotationType::Point) {
        properties.classification = m_currentCategory;
    }

    m_annotations.emplace_back(id, std::move(geometry), std::move(properties), metadata);
    emit annotationAdded(id);
    return id;
}
```

Note the ordering: `core::typeOf(geometry)` must be read **before** `std::move(geometry)`.

- [ ] **Step 5: Run the tests to watch them pass**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R app-tests --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

- [ ] **Step 7: Commit**

```bash
git add src/app/include/slideio/viewer/app/AnnotationModel.h src/app/src/AnnotationModel.cpp tests/app/AnnotationModelTest.cpp
git commit -m "Stamp the current category onto new point markers"
```

---

### Task 5: app — one undo step for a whole retag

**Files:**
- Create: `src/app/include/slideio/viewer/app/AnnotationRetagCommand.h`
- Create: `src/app/src/AnnotationRetagCommand.cpp`
- Create: `tests/app/AnnotationRetagCommandTest.cpp`
- Modify: `src/app/include/slideio/viewer/app/AnnotationModel.h` (beside `restore`, around line 86)
- Modify: `src/app/src/AnnotationModel.cpp` (beside `restore`)
- Modify: `src/app/CMakeLists.txt:6` (after `src/AnnotationEditCommand.cpp`)
- Modify: `tests/CMakeLists.txt:40` (after `app/AnnotationEditCommandTest.cpp`)

**Interfaces:**
- Consumes: `core::RetagChange` and `core::retagClassification` from Task 2; `app::ICommand` from `UndoRedoStack.h`.
- Produces: `std::size_t app::AnnotationModel::restoreMany(const std::vector<core::Annotation>&)` and `static std::unique_ptr<AnnotationRetagCommand> app::AnnotationRetagCommand::create(AnnotationModel&, std::vector<core::RetagChange>, std::string description)`.

- [ ] **Step 1: Create the files with an empty namespace and register them**

`src/app/include/slideio/viewer/app/AnnotationRetagCommand.h`:

```cpp
#pragma once

#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/AnnotationCategories.h"

#include <memory>
#include <string>
#include <vector>

namespace slideio::viewer::app
{

} // namespace slideio::viewer::app
```

`src/app/src/AnnotationRetagCommand.cpp`:

```cpp
#include "slideio/viewer/app/AnnotationRetagCommand.h"

namespace slideio::viewer::app
{

} // namespace slideio::viewer::app
```

`tests/app/AnnotationRetagCommandTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationRetagCommand.h"
```

In `src/app/CMakeLists.txt`, after line 6:

```cmake
    src/AnnotationRetagCommand.cpp
```

Do **not** add the header to the explicit header list below it: that list exists only for `Q_OBJECT` headers AUTOMOC cannot find, and this class implements `ICommand`, not `QObject`.

In `tests/CMakeLists.txt`, after line 40:

```cmake
    app/AnnotationRetagCommandTest.cpp
```

- [ ] **Step 2: Regenerate the build system**

Run: `cmake -S . -B build/build`
Expected: configures and generates without error.

- [ ] **Step 3: Write the failing tests**

Append to `tests/app/AnnotationRetagCommandTest.cpp`:

```cpp
#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/UndoRedoStack.h"
#include "slideio/viewer/core/AnnotationCategories.h"
#include "slideio/viewer/core/AnnotationGeometry.h"

#include <chrono>
#include <string>
#include <vector>

using namespace slideio::viewer::app;
namespace core = slideio::viewer::core;

namespace
{

constexpr std::chrono::system_clock::time_point kStamp{std::chrono::seconds{1'700'000'000}};

std::string addMarker(AnnotationModel& model, const std::string& classification)
{
    model.setCurrentCategory(classification);
    return model.add(core::PointGeometry{core::PointF{1.0, 2.0}});
}

} // namespace

TEST_CASE("A retag command applies every change and undoes every change",
          "[app][AnnotationRetagCommand]")
{
    AnnotationModel model;
    const std::string a = addMarker(model, "Mitossis");
    const std::string b = addMarker(model, "Mitossis");
    const std::string other = addMarker(model, "Lymphocyte");

    auto changes = core::retagClassification(model.annotations(), "Mitossis", "Mitosis", kStamp);
    REQUIRE(changes.size() == 2);

    auto command = AnnotationRetagCommand::create(model, std::move(changes), "Rename category");
    REQUIRE(command != nullptr);

    command->execute();
    REQUIRE(model.find(a)->properties().classification == "Mitosis");
    REQUIRE(model.find(b)->properties().classification == "Mitosis");
    REQUIRE(model.find(other)->properties().classification == "Lymphocyte");

    command->undo();
    REQUIRE(model.find(a)->properties().classification == "Mitossis");
    REQUIRE(model.find(b)->properties().classification == "Mitossis");
    REQUIRE(model.find(other)->properties().classification == "Lymphocyte");
}

TEST_CASE("A retag of many annotations is one step on the undo stack",
          "[app][AnnotationRetagCommand]")
{
    AnnotationModel model;
    addMarker(model, "Mitossis");
    addMarker(model, "Mitossis");
    addMarker(model, "Mitossis");

    UndoRedoStack stack;
    auto changes = core::retagClassification(model.annotations(), "Mitossis", "Mitosis", kStamp);
    stack.push(AnnotationRetagCommand::create(model, std::move(changes), "Rename category"));

    stack.undo();
    REQUIRE(model.annotations()[0].properties().classification == "Mitossis");
    REQUIRE(model.annotations()[2].properties().classification == "Mitossis");

    stack.redo();
    REQUIRE(model.annotations()[0].properties().classification == "Mitosis");
    REQUIRE(model.annotations()[2].properties().classification == "Mitosis");
}

TEST_CASE("A retag command with no changes is not built", "[app][AnnotationRetagCommand]")
{
    // The rule AnnotationEditCommand::create already applies: a command that
    // records nothing would sit on the stack as a step that does nothing, and
    // UndoRedoStack::push already ignores null.
    AnnotationModel model;
    addMarker(model, "Mitosis");

    REQUIRE(AnnotationRetagCommand::create(model, {}, "Rename category") == nullptr);
}

TEST_CASE("restoreMany emits one modelReset rather than one change per annotation",
          "[app][AnnotationModel]")
{
    // Review Focus 5: AnnotationListPanel rebuilds its whole tree on every
    // annotationChanged, so N restores would be N full rebuilds.
    AnnotationModel model;
    addMarker(model, "Mitossis");
    addMarker(model, "Mitossis");

    int resets = 0;
    int changes = 0;
    QObject::connect(&model, &AnnotationModel::modelReset, &model, [&resets]() { ++resets; });
    QObject::connect(&model, &AnnotationModel::annotationChanged, &model,
                     [&changes](const std::string&) { ++changes; });

    auto retag = core::retagClassification(model.annotations(), "Mitossis", "Mitosis", kStamp);
    std::vector<core::Annotation> after;
    for (const core::RetagChange& change : retag) {
        after.push_back(change.after);
    }

    REQUIRE(model.restoreMany(after) == 2);
    REQUIRE(resets == 1);
    REQUIRE(changes == 0);
}

TEST_CASE("restoreMany ignores an annotation the model does not hold",
          "[app][AnnotationModel]")
{
    AnnotationModel model;
    const std::string id = addMarker(model, "Mitosis");

    core::Annotation stranger("not-in-the-model", core::PointGeometry{core::PointF{0.0, 0.0}});

    REQUIRE(model.restoreMany({stranger}) == 0);
    REQUIRE(model.find(id)->properties().classification == "Mitosis");
}
```

- [ ] **Step 4: Run the tests to watch them fail**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R app-tests --output-on-failure`
Expected: the build FAILS with errors naming `AnnotationRetagCommand` and `restoreMany`.

- [ ] **Step 5: Add `restoreMany` to the model**

In `AnnotationModel.h`, immediately after the `restore` declaration:

```cpp
    /// Puts every annotation in `annotations` back under the id it carries,
    /// exactly as restore() does, and emits modelReset() ONCE. Returns how many
    /// were stored; ids the model does not hold are skipped.
    ///
    /// One signal rather than one per annotation because AnnotationListPanel
    /// rebuilds its entire tree on annotationChanged: retagging 500 markers
    /// through 500 restore() calls would rebuild a 500-row tree 500 times.
    std::size_t restoreMany(const std::vector<core::Annotation>& annotations);
```

In `AnnotationModel.cpp`, beside `restore`:

```cpp
std::size_t AnnotationModel::restoreMany(const std::vector<core::Annotation>& annotations)
{
    std::size_t stored = 0;
    for (const core::Annotation& annotation : annotations) {
        const std::size_t index = indexOf(annotation.id());
        if (index == kNotFound) {
            continue;
        }
        m_annotations[index] = annotation;
        ++stored;
    }
    if (stored > 0) {
        emit modelReset();
    }
    return stored;
}
```

`indexOf` and `kNotFound` are already public members of `AnnotationModel` (`AnnotationModel.h:44-50`). Assigning in place rather than erase-and-insert is what `restore()` does, and for the same reason: the position in the vector is paint order and hit order.

- [ ] **Step 6: Implement the command**

Inside the namespace in `AnnotationRetagCommand.h`:

```cpp
class AnnotationModel;

/// One reversible retag of MANY annotations, recorded as the states they were
/// in before and after.
///
/// AnnotationEditCommand is not reusable for this. It is one annotation by
/// construction and it carries the index the annotation sat at, because
/// restoring a created or deleted annotation at the wrong position changes
/// which shape paints on top and which one a click finds. A retag changes a
/// property in place and moves nothing, so it needs none of that machinery;
/// sharing the class would give every existing command a vector it never uses.
class AnnotationRetagCommand : public ICommand
{
public:
    /// Null when `changes` is empty, because such a command records nothing: it
    /// would sit on the stack as a step that does nothing when the user presses
    /// Ctrl+Z. Null is what UndoRedoStack::push already ignores.
    [[nodiscard]] static std::unique_ptr<AnnotationRetagCommand> create(
        AnnotationModel& model, std::vector<core::RetagChange> changes, std::string description);

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string description() const override;

private:
    AnnotationRetagCommand(AnnotationModel& model, std::vector<core::RetagChange> changes,
                           std::string description);

    void apply(bool after);

    AnnotationModel& m_model;
    std::vector<core::RetagChange> m_changes;
    std::string m_description;
};
```

`AnnotationRetagCommand.cpp`:

```cpp
#include "slideio/viewer/app/AnnotationRetagCommand.h"

#include "slideio/viewer/app/AnnotationModel.h"

#include <utility>

namespace slideio::viewer::app
{

std::unique_ptr<AnnotationRetagCommand> AnnotationRetagCommand::create(
    AnnotationModel& model, std::vector<core::RetagChange> changes, std::string description)
{
    if (changes.empty()) {
        return nullptr;
    }
    return std::unique_ptr<AnnotationRetagCommand>(
        new AnnotationRetagCommand(model, std::move(changes), std::move(description)));
}

AnnotationRetagCommand::AnnotationRetagCommand(AnnotationModel& model,
                                               std::vector<core::RetagChange> changes,
                                               std::string description)
    : m_model(model), m_changes(std::move(changes)), m_description(std::move(description))
{
}

void AnnotationRetagCommand::apply(bool after)
{
    std::vector<core::Annotation> states;
    states.reserve(m_changes.size());
    for (const core::RetagChange& change : m_changes) {
        states.push_back(after ? change.after : change.before);
    }
    m_model.restoreMany(states);
}

void AnnotationRetagCommand::execute()
{
    apply(true);
}

void AnnotationRetagCommand::undo()
{
    apply(false);
}

std::string AnnotationRetagCommand::description() const
{
    return m_description;
}

} // namespace slideio::viewer::app
```

- [ ] **Step 7: Run the tests to watch them pass**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R app-tests --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

- [ ] **Step 9: Commit**

```bash
git add src/app/include/slideio/viewer/app/AnnotationRetagCommand.h src/app/src/AnnotationRetagCommand.cpp src/app/include/slideio/viewer/app/AnnotationModel.h src/app/src/AnnotationModel.cpp tests/app/AnnotationRetagCommandTest.cpp src/app/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Record a whole retag as one undo step"
```

---

### Task 6: ui — the tally

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationListPolicy.h` (after `annotationDockTitle`, line 60)
- Modify: `src/ui/src/AnnotationListPolicy.cpp` (after `annotationDockTitle`, line 133)
- Test: `tests/ui/AnnotationListPolicyTest.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `struct ui::CategoryTallyRow { std::string category; std::size_t count; bool configured; };` and `std::vector<CategoryTallyRow> ui::categoryTally(const std::vector<core::Annotation>& annotations, const std::vector<std::string>& categories)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/ui/AnnotationListPolicyTest.cpp`, matching the file's existing `using` style:

```cpp
TEST_CASE("categoryTally lists configured categories in order, including zeros",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "Stroma")};

    const auto rows = categoryTally(annotations, {"Tumor", "Stroma"});

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].category == "Tumor");
    REQUIRE(rows[0].count == 0);
    REQUIRE(rows[0].configured);
    REQUIRE(rows[1].category == "Stroma");
    REQUIRE(rows[1].count == 1);
}

TEST_CASE("categoryTally puts unconfigured categories after the configured ones, in "
          "first-appearance order", "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{
        markerWith("a", "Zeta"), markerWith("b", "Alpha"), markerWith("c", "Tumor")};

    const auto rows = categoryTally(annotations, {"Tumor"});

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].category == "Tumor");
    REQUIRE(rows[1].category == "Zeta");
    REQUIRE_FALSE(rows[1].configured);
    REQUIRE(rows[2].category == "Alpha");
}

TEST_CASE("categoryTally never orders rows by count", "[ui][CategoryTally]")
{
    // A row that jumps while the user is clicking at it is a misclick, and this
    // is a workflow of several hundred clicks.
    const std::vector<core::Annotation> annotations{
        markerWith("a", "Stroma"), markerWith("b", "Stroma"), markerWith("c", "Tumor")};

    const auto rows = categoryTally(annotations, {"Tumor", "Stroma"});

    REQUIRE(rows[0].category == "Tumor");
    REQUIRE(rows[0].count == 1);
    REQUIRE(rows[1].category == "Stroma");
    REQUIRE(rows[1].count == 2);
}

TEST_CASE("categoryTally counts case-insensitively under the configured spelling",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "mitosis"),
                                                    markerWith("b", "MITOSIS")};

    const auto rows = categoryTally(annotations, {"Mitosis"});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "Mitosis");
    REQUIRE(rows[0].count == 2);
}

TEST_CASE("categoryTally counts an unconfigured category under its first-seen spelling",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> annotations{markerWith("a", "mitosis"),
                                                    markerWith("b", "Mitosis")};

    const auto rows = categoryTally(annotations, {});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "mitosis");
    REQUIRE(rows[0].count == 2);
}

TEST_CASE("categoryTally puts Unclassified last and only when it is non-zero",
          "[ui][CategoryTally]")
{
    const std::vector<core::Annotation> withNone{markerWith("a", "Tumor")};
    REQUIRE(categoryTally(withNone, {"Tumor"}).size() == 1);

    const std::vector<core::Annotation> withSome{markerWith("a", "Tumor"), markerWith("b", "")};
    const auto rows = categoryTally(withSome, {"Tumor"});

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[1].category == "Unclassified");
    REQUIRE(rows[1].count == 1);
    REQUIRE_FALSE(rows[1].configured);
}

TEST_CASE("categoryTally treats a whitespace-only classification as Unclassified",
          "[ui][CategoryTally]")
{
    // Review Focus 3: annotation files are hand-editable, and a row labelled
    // with three spaces is unreadable and unselectable.
    const std::vector<core::Annotation> annotations{markerWith("a", "   ")};

    const auto rows = categoryTally(annotations, {});

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].category == "Unclassified");
    REQUIRE(rows[0].count == 1);
}

TEST_CASE("categoryTally on an empty model still lists the configured categories",
          "[ui][CategoryTally]")
{
    const auto rows = categoryTally({}, {"Tumor", "Stroma"});

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].count == 0);
    REQUIRE(rows[1].count == 0);
}
```

If `tests/ui/AnnotationListPolicyTest.cpp` has no `markerWith` helper, add this one near the top of its anonymous namespace:

```cpp
core::Annotation markerWith(const std::string& id, const std::string& classification)
{
    core::Annotation annotation(id, core::PointGeometry{core::PointF{1.0, 2.0}});
    core::AnnotationProperties properties = annotation.properties();
    properties.classification = classification;
    annotation.setProperties(std::move(properties));
    return annotation;
}
```

- [ ] **Step 2: Run the tests to watch them fail**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`
Expected: the build FAILS with an error naming `categoryTally` as undeclared.

- [ ] **Step 3: Declare the type and function**

Append inside the namespace in `AnnotationListPolicy.h`:

```cpp
/// One row of the counting strip's tally.
struct CategoryTallyRow
{
    std::string category;  ///< Display text; "Unclassified" for the empty one.
    std::size_t count;
    bool configured;  ///< In the user's list, as opposed to only in the data.
};

/// The counting tallies, in display order.
///
/// Configured categories first, in `categories` order, INCLUDING those at zero,
/// so a category about to be counted has a row before counting starts. Then
/// categories present in the data but not configured, in first-appearance order
/// over `annotations`. Then Unclassified, only when non-zero.
///
/// Rows are NEVER ordered by count. A row that jumps while the user is clicking
/// at it is a misclick, and this is a workflow of several hundred clicks -- the
/// same reasoning that makes annotationRows() break every sort tie by creation
/// order so rows never swap between rebuilds.
///
/// Matching is case-insensitive; a configured spelling wins, otherwise the
/// first one seen. A classification that is empty or only whitespace counts as
/// Unclassified: annotation files are hand-editable and a row labelled with
/// three spaces is unreadable.
///
/// It takes no filter text, deliberately: the counts are over every annotation
/// in the scene, never the filtered subset. A clinical count that changed when
/// someone typed in a search box would be a trap, and annotationDockTitle()
/// already distinguishes the two populations.
[[nodiscard]] std::vector<CategoryTallyRow> categoryTally(
    const std::vector<core::Annotation>& annotations,
    const std::vector<std::string>& categories);
```

- [ ] **Step 4: Implement it**

The file already has a `toLower` helper at line 15 but no trim helper, so add this one beside it in the same anonymous namespace:

```cpp
std::string trimmedText(const std::string& text)
{
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto first = std::find_if(text.begin(), text.end(), notSpace);
    const auto last = std::find_if(text.rbegin(), text.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}
```

Then append the function itself inside the namespace, after `annotationDockTitle`:

```cpp
std::vector<CategoryTallyRow> categoryTally(const std::vector<core::Annotation>& annotations,
                                            const std::vector<std::string>& categories)
{
    std::vector<CategoryTallyRow> rows;
    std::vector<std::string> keys;  // Parallel to rows, lowercased for matching.
    for (const std::string& category : categories) {
        const std::string clean = trimmedText(category);
        if (clean.empty()) {
            continue;
        }
        const std::string key = toLower(clean);
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
            continue;
        }
        keys.push_back(key);
        rows.push_back(CategoryTallyRow{clean, 0, true});
    }

    std::size_t unclassified = 0;
    for (const core::Annotation& annotation : annotations) {
        const std::string clean = trimmedText(annotation.properties().classification);
        if (clean.empty()) {
            ++unclassified;
            continue;
        }
        const std::string key = toLower(clean);
        const auto found = std::find(keys.begin(), keys.end(), key);
        if (found != keys.end()) {
            ++rows[static_cast<std::size_t>(std::distance(keys.begin(), found))].count;
            continue;
        }
        // Not configured: it joins the end, keeping the spelling first seen.
        keys.push_back(key);
        rows.push_back(CategoryTallyRow{clean, 1, false});
    }

    if (unclassified > 0) {
        rows.push_back(CategoryTallyRow{"Unclassified", unclassified, false});
    }
    return rows;
}
```

- [ ] **Step 5: Run the tests to watch them pass**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release -R ui-tests --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationListPolicy.h src/ui/src/AnnotationListPolicy.cpp tests/ui/AnnotationListPolicyTest.cpp
git commit -m "Tally annotations by category in display order"
```

---

### Task 7: ui — one vocabulary, edited in Preferences

Everything from here on is unreachable by every suite. Verify by running the application, and record what is observed in group AC afterwards.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/PreferencesDialog.h:18-22`
- Modify: `src/ui/src/PreferencesDialog.cpp`
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationPropertiesEdit.h:13-17`
- Modify: `src/ui/src/AnnotationPropertiesPanel.cpp:99-107`
- Modify: `src/ui/src/MainWindow.cpp` (the Impl's members near line 212; the preferences action; panel construction at 1876-1882)

**Interfaces:**
- Consumes: `infra::QSettingsAnnotationCategoryStore` from Task 3.
- Produces: `void AnnotationPropertiesPanel::setCategories(const std::vector<std::string>&)`; `PreferencesDialog` constructor taking categories and a `QStringList categories() const` getter.

- [ ] **Step 1: Retire the hardcoded array as the source of truth**

In `AnnotationPropertiesEdit.h`, replace the comment above `kAnnotationClassifications` so it states what the array is now for. The array itself stays — it is the seed documented in `QSettingsAnnotationCategoryStore::defaultCategories()`:

```cpp
/// The seed for QSettingsAnnotationCategoryStore::defaultCategories(), kept
/// here because this is where the vocabulary used to live and a reader looking
/// for it will come here first. It is NOT the source of truth: both the
/// properties panel's combo and the counting strip read the user's configured
/// list from the store, or the two panels would disagree about the vocabulary.
/// classification remains a plain string in the stored file, so the serializer
/// and schemaVersion are untouched by what is listed anywhere.
inline constexpr std::array<const char*, 5> kAnnotationClassifications{
    "Tumor", "Stroma", "Necrosis", "Normal", "Artifact"};
```

- [ ] **Step 2: Give the properties panel a settable category list**

In `AnnotationPropertiesPanel`, add a public method and have it rebuild the combo's items, preserving the current text:

```cpp
void AnnotationPropertiesPanel::setCategories(const std::vector<std::string>& categories)
{
    const QString current = m_impl->classification->currentText();
    m_impl->classification->clear();
    for (const std::string& category : categories) {
        m_impl->classification->addItem(QString::fromStdString(category));
    }
    // The combo stays editable, so a classification already on an annotation is
    // still shown even when it is not in the list.
    m_impl->classification->setCurrentText(current);
}
```

Replace the `kAnnotationClassifications` loop at `AnnotationPropertiesPanel.cpp:101-103` with nothing — the items now arrive through `setCategories`.

- [ ] **Step 3: Add the Preferences row**

In `PreferencesDialog.h`, extend the constructor and add a getter:

```cpp
    PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                      const QStringList& categories, QWidget* parent = nullptr);

    [[nodiscard]] QStringList categories() const;
```

In `PreferencesDialog.cpp`, add a `QPlainTextEdit` holding one category per line, labelled `tr("Annotation categories:")`, with the hint `tr("One per line. Used for counting and for the Classification field.")`. The getter splits on newlines; normalisation happens in the store, so the dialog does not need to do it:

```cpp
QStringList PreferencesDialog::categories() const
{
    return m_impl->categories->toPlainText().split('\n', Qt::SkipEmptyParts);
}
```

- [ ] **Step 4: Own the store in MainWindow and feed both panels**

Beside `overrideStore` in the Impl (around `MainWindow.cpp:212`):

```cpp
    std::unique_ptr<infra::QSettingsAnnotationCategoryStore> categoryStore;
```

After the panels are constructed (`MainWindow.cpp:1876-1882`), push the list into the properties panel:

```cpp
    m_impl->annotationPropertiesPanel->setCategories(m_impl->categoryStore->categories());
```

Where the Preferences dialog is shown, pass the list in and write back what comes out, then re-feed both panels:

```cpp
    std::vector<std::string> edited;
    for (const QString& entry : dialog.categories()) {
        edited.push_back(entry.toStdString());
    }
    m_impl->categoryStore->setCategories(edited);
    const std::vector<std::string> stored = m_impl->categoryStore->categories();
    m_impl->annotationPropertiesPanel->setCategories(stored);
    m_impl->annotationListPanel->setCategories(stored);
```

`AnnotationListPanel::setCategories` arrives in Task 8; until then this line will not compile, so add it in Task 8 rather than here.

- [ ] **Step 5: Build, run the suite, and drive the application**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4` — no suite reaches this code, so a green run only proves nothing else broke.

Then kill any running viewer, run `cmake --install build/build --config Release`, and compare `sha256sum build/install/release/bin/slideio-viewer.exe build/build/Release/slideio-viewer.exe` — a running viewer makes the install a silent no-op that still exits 0, so the artifact is what tells you, not the exit code. Open Preferences and confirm the five defaults appear, edit them, reopen, and confirm the properties panel's Classification combo offers the edited list (AC1, AC2, AC4).

- [ ] **Step 6: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/PreferencesDialog.h src/ui/src/PreferencesDialog.cpp src/ui/include/slideio/viewer/ui/AnnotationPropertiesEdit.h src/ui/src/AnnotationPropertiesPanel.cpp src/ui/include/slideio/viewer/ui/AnnotationPropertiesPanel.h src/ui/src/MainWindow.cpp
git commit -m "Let the user configure the annotation category list"
```

---

### Task 8: ui — the counting strip

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationListPanel.h`
- Modify: `src/ui/src/AnnotationListPanel.cpp` (construction near line 43-73; `setAnnotationModel` at 95-118; `rebuild` at 153-175)
- Modify: `src/ui/src/MainWindow.cpp` (`applyAnnotationAvailability` at 1716-1749; panel construction at 1876)

**Interfaces:**
- Consumes: `ui::categoryTally` (Task 6), `app::AnnotationModel::setCurrentCategory` (Task 4), `infra::QSettingsAnnotationCategoryStore` (Task 3).
- Produces: `void AnnotationListPanel::setCategories(const std::vector<std::string>&)`, `void AnnotationListPanel::setCountingEnabled(bool)`, and the signal `void categoryUsed(const std::string& category)`.

- [ ] **Step 1: Add the strip's widgets**

Above the existing filter box in the panel's layout. `(none)` is not decoration: without it there is no way back to placing unclassified markers short of restarting.

```cpp
    m_impl->counting = new QComboBox(contents);
    m_impl->counting->setEditable(true);
    m_impl->counting->setToolTip(tr("New point markers are counted under this category"));

    m_impl->tally = new QTreeWidget(contents);
    m_impl->tally->setColumnCount(2);
    m_impl->tally->setHeaderHidden(true);
    m_impl->tally->setRootIsDecorated(false);
    m_impl->tally->setSelectionMode(QAbstractItemView::NoSelection);
    m_impl->tally->setFocusPolicy(Qt::NoFocus);
    // The strip must not grow at the list's expense as categories accumulate.
    m_impl->tally->setMaximumHeight(140);

    auto* countingRow = new QFormLayout;
    countingRow->addRow(tr("Counting:"), m_impl->counting);
    layout->insertLayout(0, countingRow);
    layout->insertWidget(1, m_impl->tally);
```

And a `setCategories` that rebuilds the combo, keeping `(none)` first and preserving what is current:

```cpp
void AnnotationListPanel::setCategories(const std::vector<std::string>& categories)
{
    const QString current = m_impl->counting->currentText();
    const bool wasNone = m_impl->counting->currentIndex() == 0;
    m_impl->categories = categories;

    QSignalBlocker blocker(m_impl->counting);  // Rebuilding must not restamp the model.
    m_impl->counting->clear();
    m_impl->counting->addItem(tr("(none)"));
    for (const std::string& category : categories) {
        m_impl->counting->addItem(QString::fromStdString(category));
    }
    if (wasNone) {
        m_impl->counting->setCurrentIndex(0);
    } else {
        m_impl->counting->setCurrentText(current);
    }
    rebuild();
}

void AnnotationListPanel::setCountingEnabled(bool enabled)
{
    m_impl->counting->setEnabled(enabled);
    // The tally stays visible and correct: a read-only slide still shows its
    // annotations, and the count of what is there is what the reader wants.
}
```

The `QSignalBlocker` matters: without it, rebuilding the combo after a category joins the list would fire `currentTextChanged` and restamp the model's current category mid-count.

- [ ] **Step 2: Rebuild the tally wherever the rows rebuild**

`rebuild()` already runs on every model signal (`AnnotationListPanel.cpp:110-114`). Add the tally to it:

```cpp
    const std::vector<CategoryTallyRow> tallyRows =
        categoryTally(m_impl->model->annotations(), m_impl->categories);
    m_impl->tally->clear();
    for (const CategoryTallyRow& row : tallyRows) {
        auto* item = new QTreeWidgetItem(m_impl->tally);
        item->setText(0, QString::fromStdString(row.category));
        item->setText(1, QString::number(static_cast<qulonglong>(row.count)));
        item->setData(0, Qt::UserRole, row.configured);
    }
```

The tally is built from `m_impl->model->annotations()` and never from the filtered rows: the counts are over the whole scene (spec §5.3).

- [ ] **Step 3: Report the current category to the model**

On the combo's `currentTextChanged`, set the model's current category — `(none)` maps to an empty string:

```cpp
    connect(m_impl->counting, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        const bool none = m_impl->counting->currentIndex() == 0;
        const std::string category = none ? std::string{} : text.trimmed().toStdString();
        if (m_impl->model != nullptr) {
            m_impl->model->setCurrentCategory(category);
        }
    });
```

- [ ] **Step 4: Emit `categoryUsed` when a marker is counted under a new category**

In the `annotationAdded` handler, after the rebuild, emit the signal when the added annotation carries a non-empty classification:

```cpp
    connect(m_impl->model, &app::AnnotationModel::annotationAdded, this,
            [this](const std::string& id) {
                rebuild();
                const core::Annotation* added = m_impl->model->find(id);
                if (added != nullptr && !added->properties().classification.empty()) {
                    emit categoryUsed(added->properties().classification);
                }
            });
```

This replaces the shared `rebuildRows` lambda for `annotationAdded` only; the other four signals keep it.

- [ ] **Step 5: Join the list on first use, in MainWindow**

Connect `categoryUsed` so a category that has counted something joins the configured list, and persist it:

```cpp
    QObject::connect(annotationListPanel, &AnnotationListPanel::categoryUsed, owner,
                     [this](const std::string& category) {
                         std::vector<std::string> list = categoryStore->categories();
                         // normalizeCategoryList is the one rule for "is this
                         // already in the list": appending and re-normalising
                         // drops a case-insensitive duplicate for us, so there
                         // is no second comparison to keep in step with it.
                         std::vector<std::string> candidate = list;
                         candidate.push_back(category);
                         if (core::normalizeCategoryList(candidate).size() == list.size()) {
                             return;  // Already there, in some spelling.
                         }
                         list.push_back(category);
                         categoryStore->setCategories(list);
                         const std::vector<std::string> stored = categoryStore->categories();
                         annotationListPanel->setCategories(stored);
                         annotationPropertiesPanel->setCategories(stored);
                     });
```

A category earns its place by having something counted under it. Typing alone must not add it: a typo noticed before the first click then leaves nothing behind.

- [ ] **Step 6: Gate the combo on a read-only slide, and only the combo**

In `applyAnnotationAvailability` (`MainWindow.cpp:1718-1730`), beside the existing `annotationPropertiesPanel->setEditingAllowed(available)`:

```cpp
    if (annotationListPanel != nullptr) {
        annotationListPanel->setCountingEnabled(available);
    }
```

`setCountingEnabled(false)` disables the combo **and leaves the tally rows visible and correct**: a read-only slide still shows its annotations, and the count of what is there is exactly what someone opening one read-only wants to see.

- [ ] **Step 7: Add the Task 7 line that could not compile yet**

In the Preferences handler added in Task 7 Step 4, the line
`m_impl->annotationListPanel->setCategories(stored);` now compiles. Add it.

- [ ] **Step 8: Build, run the suite, and drive the application**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

Install (killing any running viewer first) and verify by hash as in Task 7 Step 5, then check AC5-AC14 and AC22 by hand: pick a category and place five markers; draw a rectangle and an arrow and confirm both are unclassified; change the category and confirm the five are unchanged; select `(none)` and confirm Unclassified appears; confirm configured categories show rows at zero; type a category and confirm it does not join the list until the first marker; type in the filter box and confirm the tally does not move.

- [ ] **Step 9: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationListPanel.h src/ui/src/AnnotationListPanel.cpp src/ui/src/MainWindow.cpp
git commit -m "Count markers by category in the annotation panel"
```

---

### Task 9: ui — rename and remove from the tally row

**Files:**
- Modify: `src/ui/src/AnnotationListPanel.cpp` (the tally widget)
- Modify: `src/ui/include/slideio/viewer/ui/AnnotationListPanel.h`
- Modify: `src/ui/src/MainWindow.cpp`

**Interfaces:**
- Consumes: `core::retagClassification` (Task 2), `app::AnnotationRetagCommand` (Task 5), `infra::QSettingsAnnotationCategoryStore` (Task 3).
- Produces: the signals `void renameCategoryRequested(const std::string& category)` and `void removeCategoryRequested(const std::string& category)`.

- [ ] **Step 1: Add the context menu**

Set `m_impl->tally->setContextMenuPolicy(Qt::CustomContextMenu)` and build a menu on `customContextMenuRequested`:

- **Rename…** — offered for every row except Unclassified. Renaming Unclassified would mean bulk-classifying every unclassified annotation on the scene, from a menu item reading "Rename": the single most destructive thing this feature could offer.
- **Remove from list** — offered only when the row's `configured` flag (stored in `Qt::UserRole` in Task 8 Step 2) is true. Its text states how many annotations keep the category, so the difference between removing the suggestion and fixing the count is visible at the point of decision:

```cpp
    const QString removeText = count == 0
        ? tr("Remove from list")
        : tr("Remove from list (%n annotation(s) keep it)", "", static_cast<int>(count));
```

- [ ] **Step 2: Ask for the new name**

On **Rename…**, emit `renameCategoryRequested` with the row's category. The panel does not open the dialog: `MainWindow` owns the store, the model and the undo stack, and splitting the work would put half the rename in a panel that cannot finish it.

```cpp
    connect(renameAction, &QAction::triggered, this, [this, category]() {
        emit renameCategoryRequested(category);
    });
```

- [ ] **Step 3: Handle the rename in MainWindow**

```cpp
    bool accepted = false;
    const QString typed = QInputDialog::getText(
        owner, tr("Rename Category"), tr("New name for \"%1\":").arg(QString::fromStdString(from)),
        QLineEdit::Normal, QString::fromStdString(from), &accepted);
    if (!accepted) {
        return;
    }
    const std::string target = typed.trimmed().toStdString();
    if (target.empty()) {
        statusBarManager->showTransientMessage(
            tr("A category needs a name. Nothing was renamed."));
        return;
    }
    auto changes = core::retagClassification(annotationModel->annotations(), from, target,
                                             std::chrono::system_clock::now());
    if (!changes.empty()) {
        undoStack->push(app::AnnotationRetagCommand::create(
            *annotationModel, std::move(changes),
            tr("Rename Category").toStdString()));
    }
```

`retagClassification` returns empty when the name did not change, and an empty result records no undo step at all — a step that does nothing is worse than no step, because the user assumes it worked.

Then rename the entry in the stored list, keeping its position, and re-feed both panels. **If the renamed category was the current one, carry the current category over**, so counting continues under the new name without the user re-picking.

Only the open scene is retagged. Other slides keep the old string, which §6.4 of the spec states rather than implies.

- [ ] **Step 4: Handle the removal in MainWindow**

Remove the entry from the stored list, persist, and re-feed both panels. **Never touch the annotations.** A category removed from the list while annotations still carry it keeps its tally row, now unconfigured — which is already how an ad-hoc category behaves. If it was the current category it stays current: a value may be current without being configured, which is already true of one typed before its first marker.

- [ ] **Step 5: Build, run the suite, and drive the application**

Run: `cmake --build build/build --config Release && ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

Install and verify by hash, then check AC15-AC19 and AC24 by hand: count under two categories and rename one onto the other, confirming the rows merge and the sum is right; Ctrl+Z and Ctrl+Y and confirm one step each way; confirm an empty target is refused and says why; confirm Unclassified offers no Rename; confirm Remove from list states the count, removes the suggestion and leaves the annotations; rename the current category and confirm counting continues under the new name.

- [ ] **Step 6: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationListPanel.h src/ui/src/AnnotationListPanel.cpp src/ui/src/MainWindow.cpp
git commit -m "Rename a category and retag what carries it"
```

---

### Task 10: Record what changed, and correct what this falsifies

**Files:**
- Modify: `CLAUDE.md` (the undo/redo bullet; the annotation bullets)
- Modify: `docs/superpowers/specs/2026-10-18-annotation-categorized-counting-design.md` (the status line and §7.5)
- Check: `documents/implementation-stages.md`

- [ ] **Step 1: Correct the "one command type" claim**

`CLAUDE.md`'s undo/redo bullet says one command type covers create, delete and move "because the inverse is the same shape in all three". `AnnotationRetagCommand` makes that incomplete. Replace that clause with:

```markdown
  One command type covers create, delete and move, because the inverse is the
  same shape in all three: the state before and the state after. Single
  property edits fit it unchanged. **A second type, `AnnotationRetagCommand`,
  covers the one case it cannot: many annotations in one step.**
  `AnnotationEditCommand` is one annotation by construction and carries the
  index the annotation sat at, because restoring a created or deleted
  annotation at the wrong position changes which shape paints on top and which
  one a click finds; a retag changes a property in place and moves nothing.
  It restores through `AnnotationModel::restoreMany`, which emits one
  `modelReset` rather than one `annotationChanged` per annotation, because
  `AnnotationListPanel` rebuilds its whole tree on that signal and retagging
  500 markers would otherwise rebuild a 500-row tree 500 times.
```

- [ ] **Step 2: Add the categorized-counting decisions**

Add a Key Design Decision bullet covering: the counting category **is** `classification` and not a second axis, so the file format and `schemaVersion` are untouched; the current category is stamped in `AnnotationModel::add()` keyed on `core::typeOf(geometry) == Point` rather than on the active tool, because app-tests can reach the model and nothing can reach `ViewportWidget`; the user's category list lives in `infra::QSettingsAnnotationCategoryStore` with no core interface, because nothing in core needs it; and `ui::categoryTally` never orders rows by count and never sees the filter, because a row that jumps mid-click is a misclick and a clinical count that changed with a search box would be a trap.

- [ ] **Step 3: Flip the spec's status line and record group AC**

Change the spec's `Status:` line from "not yet implemented" to the commit range this plan produced, and record group AC's results in §7.5 the way group AB's are recorded — what was observed, not what was intended, naming the installed binary's sha256 and the commit it was built from.

- [ ] **Step 4: Check the stages document**

Read `documents/implementation-stages.md` and see whether this round maps to a listed stage item. Mark it only if it does; do not invent an entry. The point/arrow round deliberately left this file untouched for exactly that reason.

- [ ] **Step 5: Run the whole suite**

Run: `ctest --test-dir build/build -C Release --output-on-failure`
Expected: `100% tests passed out of 4`.

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs/2026-10-18-annotation-categorized-counting-design.md
git commit -m "Record categorized counting and correct the one-command-type claim"
```
