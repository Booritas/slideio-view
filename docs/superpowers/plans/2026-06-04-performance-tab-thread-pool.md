# Settings Performance Tab (Image-Reading Thread Pool) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Performance" tab to the Settings dialog that sets the image-reading thread-pool size (1–32, default 4); one value drives both the `SlideIOAdapterPool` size and the `TileLoadScheduler` worker count, persisted via `QSettings`, effective on the next opened slide.

**Architecture:** A new `ReadingSettings` module (pure `clampThreadPoolSize` + QSettings glue, mirroring `LogSettings`) holds the value. A new `PerformanceTab` widget (PIMPL, like `LogsTab`) edits it and persists on `apply()`. `SettingsDialog` hosts it as a second tab. `ViewportWidget` reads the value when constructing the adapter pool and passes the pool size as the scheduler's worker count.

**Tech Stack:** C++17, Qt 6 Widgets, QSettings, Catch2 (tests), CMake + Conan.

**Build/test commands** (this machine — `conan` is not on the default PATH):
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build -C Release --output-on-failure
```
A successful build exits 0 and prints `-- Installing: .../slideio-viewer.exe`. A trailing `cmake_install.cmake` "cannot copy ... slideio-viewer.exe / Permission denied" means a viewer is running and locking the file — the compile is what matters (no `error C####`, no other `CMake Error`).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/ui/include/slideio/viewer/ui/ReadingSettings.h` (new) | Declares `clampThreadPoolSize` (pure), `readThreadPoolSize`/`saveThreadPoolSize` (QSettings), and the min/max/default constants. |
| `src/ui/src/ReadingSettings.cpp` (new) | Implements them. Qt-light (only `<QSettings>`). |
| `tests/ui/ReadingSettingsTest.cpp` (new) | Catch2 unit tests for `clampThreadPoolSize`. |
| `src/ui/include/slideio/viewer/ui/PerformanceTab.h` (new) | `PerformanceTab : QWidget`, PIMPL, `apply()`. |
| `src/ui/src/PerformanceTab.cpp` (new) | Builds the spin box + note; `apply()` persists. |
| `src/ui/src/SettingsDialog.cpp` (modify) | Add the Performance tab; OK/Apply call its `apply()`. |
| `src/ui/src/ViewportWidget.cpp` (modify) | Use `readThreadPoolSize()` at the two pool sites; pass `poolSize()` as scheduler workers. |
| `src/ui/CMakeLists.txt` (modify) | Add `ReadingSettings.cpp`, `PerformanceTab.cpp`. |
| `tests/CMakeLists.txt` (modify) | Add `ReadingSettingsTest.cpp` + `ReadingSettings.cpp` to `slideio-viewer-ui-tests`. |

---

## Task 1: ReadingSettings module + unit test

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/ReadingSettings.h`, `src/ui/src/ReadingSettings.cpp`, `tests/ui/ReadingSettingsTest.cpp`
- Modify: `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/ui/ReadingSettingsTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/ReadingSettings.h"

using namespace slideio::viewer::ui;

TEST_CASE("clampThreadPoolSize clamps below the minimum", "[ui][ReadingSettings]")
{
    REQUIRE(clampThreadPoolSize(0) == kMinThreadPoolSize);
    REQUIRE(clampThreadPoolSize(-5) == kMinThreadPoolSize);
}

TEST_CASE("clampThreadPoolSize clamps above the maximum", "[ui][ReadingSettings]")
{
    REQUIRE(clampThreadPoolSize(33) == kMaxThreadPoolSize);
    REQUIRE(clampThreadPoolSize(1000) == kMaxThreadPoolSize);
}

TEST_CASE("clampThreadPoolSize leaves in-range values unchanged", "[ui][ReadingSettings]")
{
    REQUIRE(clampThreadPoolSize(kMinThreadPoolSize) == kMinThreadPoolSize);
    REQUIRE(clampThreadPoolSize(4) == 4);
    REQUIRE(clampThreadPoolSize(kMaxThreadPoolSize) == kMaxThreadPoolSize);
}
```

- [ ] **Step 2: Wire the test source into CMake**

In `tests/CMakeLists.txt`, add the new test file and the standalone-compiled source to the `slideio-viewer-ui-tests` executable. Replace:

```cmake
add_executable(slideio-viewer-ui-tests
    ui/LogSettingsTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/src/LogSettings.cpp
)
```

with:

```cmake
add_executable(slideio-viewer-ui-tests
    ui/LogSettingsTest.cpp
    ui/ReadingSettingsTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/src/LogSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/src/ReadingSettings.cpp
)
```

- [ ] **Step 3: Run the build to verify it fails**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: FAIL — `ReadingSettings.h` not found / `clampThreadPoolSize` undefined.

- [ ] **Step 4: Create the header**

Create `src/ui/include/slideio/viewer/ui/ReadingSettings.h`:

```cpp
#pragma once

namespace slideio::viewer::ui
{

// Image-reading thread-pool size bounds and default.
inline constexpr int kDefaultThreadPoolSize = 4;
inline constexpr int kMinThreadPoolSize = 1;
inline constexpr int kMaxThreadPoolSize = 32;

// Clamps a requested thread-pool size to [kMinThreadPoolSize, kMaxThreadPoolSize].
int clampThreadPoolSize(int n);

// Reads the persisted thread-pool size (QSettings key "reading/threadPoolSize"),
// clamped; returns kDefaultThreadPoolSize when unset.
int readThreadPoolSize();

// Persists the thread-pool size (clamped) to QSettings.
void saveThreadPoolSize(int n);

} // namespace slideio::viewer::ui
```

- [ ] **Step 5: Create the implementation**

Create `src/ui/src/ReadingSettings.cpp`:

```cpp
#include "slideio/viewer/ui/ReadingSettings.h"

#include <QSettings>
#include <QString>

#include <algorithm>

namespace
{
const QString kThreadPoolSizeKey = "reading/threadPoolSize";
} // anonymous namespace

namespace slideio::viewer::ui
{

int clampThreadPoolSize(int n)
{
    return std::clamp(n, kMinThreadPoolSize, kMaxThreadPoolSize);
}

int readThreadPoolSize()
{
    QSettings settings;
    if (!settings.contains(kThreadPoolSizeKey)) {
        return kDefaultThreadPoolSize;
    }
    return clampThreadPoolSize(settings.value(kThreadPoolSizeKey).toInt());
}

void saveThreadPoolSize(int n)
{
    QSettings settings;
    settings.setValue(kThreadPoolSizeKey, clampThreadPoolSize(n));
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 6: Add the source to the ui library**

In `src/ui/CMakeLists.txt`, add `src/ReadingSettings.cpp` right after `src/LogSettings.cpp` (line 3):

```cmake
    src/AppPaths.cpp
    src/LogSettings.cpp
    src/ReadingSettings.cpp
    src/AssociatedImageWindow.cpp
```

- [ ] **Step 7: Build and run the ui tests**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build -C Release -R ui-tests --output-on-failure
```
Expected: PASS — the three `ReadingSettings` cases plus the existing `LogSettings` cases.

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ReadingSettings.h src/ui/src/ReadingSettings.cpp \
        tests/ui/ReadingSettingsTest.cpp src/ui/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add ReadingSettings (image-reading thread-pool size, clamped, persisted)"
```

---

## Task 2: PerformanceTab widget

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/PerformanceTab.h`, `src/ui/src/PerformanceTab.cpp`
- Modify: `src/ui/CMakeLists.txt`

No unit test: Qt widget (consistent with `LogsTab`). Verified by build + Task 5.

- [ ] **Step 1: Create the header**

Create `src/ui/include/slideio/viewer/ui/PerformanceTab.h`:

```cpp
#pragma once

#include <QWidget>

#include <memory>

namespace slideio::viewer::ui
{

// The "Performance" tab of the Settings dialog: configures the image-reading
// thread-pool size. The chosen value is persisted by apply() and takes effect
// on the next opened slide.
class PerformanceTab : public QWidget
{
    Q_OBJECT

public:
    explicit PerformanceTab(QWidget* parent = nullptr);
    ~PerformanceTab() override;

    PerformanceTab(const PerformanceTab&) = delete;
    PerformanceTab& operator=(const PerformanceTab&) = delete;

    // Persist the current selection to QSettings.
    void apply();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
```

- [ ] **Step 2: Create the implementation**

Create `src/ui/src/PerformanceTab.cpp`:

```cpp
#include "slideio/viewer/ui/PerformanceTab.h"

#include "slideio/viewer/ui/ReadingSettings.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct PerformanceTab::Impl
{
    QSpinBox* threadsSpin = nullptr;
};

PerformanceTab::PerformanceTab(QWidget* parent)
    : QWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->threadsSpin = new QSpinBox(this);
    m_impl->threadsSpin->setRange(kMinThreadPoolSize, kMaxThreadPoolSize);
    m_impl->threadsSpin->setValue(readThreadPoolSize());

    auto* form = new QFormLayout;
    form->addRow("Reading threads:", m_impl->threadsSpin);

    auto* group = new QGroupBox("Image reading", this);
    group->setLayout(form);

    auto* note = new QLabel("Applies to the next opened slide.", this);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(group);
    layout->addWidget(note);
    layout->addStretch();
}

PerformanceTab::~PerformanceTab() = default;

void PerformanceTab::apply()
{
    saveThreadPoolSize(m_impl->threadsSpin->value());
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 3: Add the source to the ui library**

In `src/ui/CMakeLists.txt`, add `src/PerformanceTab.cpp` right after `src/LogsTab.cpp`:

```cmake
    src/SettingsDialog.cpp
    src/LogsTab.cpp
    src/PerformanceTab.cpp
    src/MainWindow.cpp
```

- [ ] **Step 4: Build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: clean compile (AUTOMOC picks up the new `Q_OBJECT` header; the tab compiles but is not yet shown).

- [ ] **Step 5: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/PerformanceTab.h src/ui/src/PerformanceTab.cpp \
        src/ui/CMakeLists.txt
git commit -m "Add PerformanceTab (image-reading thread-pool spin box)"
```

---

## Task 3: Add the Performance tab to SettingsDialog

**Files:**
- Modify: `src/ui/src/SettingsDialog.cpp`

No unit test: dialog wiring. Verified by build + Task 5.

- [ ] **Step 1: Add the include**

In `src/ui/src/SettingsDialog.cpp`, after `#include "slideio/viewer/ui/LogsTab.h"`:

```cpp
#include "slideio/viewer/ui/LogsTab.h"
#include "slideio/viewer/ui/PerformanceTab.h"
```

- [ ] **Step 2: Add the member to Impl**

Replace:

```cpp
struct SettingsDialog::Impl
{
    QTabWidget* tabs = nullptr;
    LogsTab* logsTab = nullptr;
};
```

with:

```cpp
struct SettingsDialog::Impl
{
    QTabWidget* tabs = nullptr;
    LogsTab* logsTab = nullptr;
    PerformanceTab* performanceTab = nullptr;
};
```

- [ ] **Step 3: Construct and add the tab**

Replace:

```cpp
    m_impl->logsTab = new LogsTab(this);
    m_impl->tabs = new QTabWidget(this);
    m_impl->tabs->addTab(m_impl->logsTab, "Logs");
```

with:

```cpp
    m_impl->logsTab = new LogsTab(this);
    m_impl->performanceTab = new PerformanceTab(this);
    m_impl->tabs = new QTabWidget(this);
    m_impl->tabs->addTab(m_impl->logsTab, "Logs");
    m_impl->tabs->addTab(m_impl->performanceTab, "Performance");
```

- [ ] **Step 4: Apply both tabs on OK and Apply**

Replace:

```cpp
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        m_impl->logsTab->apply();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        m_impl->logsTab->apply();
    });
```

with:

```cpp
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        m_impl->logsTab->apply();
        m_impl->performanceTab->apply();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        m_impl->logsTab->apply();
        m_impl->performanceTab->apply();
    });
```

- [ ] **Step 5: Build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: clean compile. The Settings dialog now shows a "Performance" tab.

- [ ] **Step 6: Commit**

```bash
git add src/ui/src/SettingsDialog.cpp
git commit -m "Add Performance tab to the Settings dialog"
```

---

## Task 4: Consume the setting in ViewportWidget

**Files:**
- Modify: `src/ui/src/ViewportWidget.cpp` (include; pool sites ~1067, ~1120; scheduler ~2107)

No unit test: integration with the open path on worker threads; verified by Task 5.

- [ ] **Step 1: Add the include**

In `src/ui/src/ViewportWidget.cpp`, after the existing
`#include "slideio/viewer/ui/ViewportController.h"` (line 2):

```cpp
#include "slideio/viewer/ui/ViewportController.h"
#include "slideio/viewer/ui/ReadingSettings.h"
```

- [ ] **Step 2: Use the setting at the scene pool site**

Replace (in `openSceneSync`, ~line 1067):

```cpp
        r.adapterPool = std::make_shared<infra::SlideIOAdapterPool>(filePath, sceneIndex, 4, driverId);
```

with:

```cpp
        r.adapterPool = std::make_shared<infra::SlideIOAdapterPool>(
            filePath, sceneIndex, slideio::viewer::ui::readThreadPoolSize(), driverId);
```

> `openSceneSync` is in the file's anonymous namespace, so the call must be fully
> qualified `slideio::viewer::ui::readThreadPoolSize()`.

- [ ] **Step 3: Use the setting at the aux-image pool site**

Replace (in `openAuxImageSync`, ~line 1120):

```cpp
        r.adapterPool = std::make_shared<infra::SlideIOAdapterPool>(filePath, auxImageName, 4, driverId);
```

with:

```cpp
        r.adapterPool = std::make_shared<infra::SlideIOAdapterPool>(
            filePath, auxImageName, slideio::viewer::ui::readThreadPoolSize(), driverId);
```

- [ ] **Step 4: Match the scheduler worker count to the pool size**

Replace (in `installSceneOpenResult`, ~line 2107):

```cpp
    m_impl->scheduler = std::make_shared<infra::TileLoadScheduler>(
        m_impl->adapterPool, m_impl->tileCache);
```

with:

```cpp
    m_impl->scheduler = std::make_shared<infra::TileLoadScheduler>(
        m_impl->adapterPool, m_impl->tileCache, m_impl->adapterPool->poolSize());
```

> `installSceneOpenResult` is a `ViewportWidget` member (ui namespace); it reads the pool
> size from the already-constructed pool rather than re-reading QSettings, so the pool and
> the scheduler always use the same number.

- [ ] **Step 5: Build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: clean compile.

- [ ] **Step 6: Run all tests**

Run:
```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: 3/3 pass (`core-tests`, `infra-tests`, `ui-tests`).

- [ ] **Step 7: Commit**

```bash
git add src/ui/src/ViewportWidget.cpp
git commit -m "Use configured thread-pool size for the adapter pool and scheduler"
```

---

## Task 5: Manual verification

**Files:** none (runtime check). Use the installed build at
`build/install/release/bin/slideio-viewer.exe`. Log file:
`%LOCALAPPDATA%\SlideIO\SlideIO Viewer\logs\slideio-viewer.log`.

- [ ] **Step 1: Default**

Launch the viewer. Tools → Settings → **Performance** tab shows "Reading threads" = **4**
and the note "Applies to the next opened slide." Open a slide; the log shows
`SlideIOAdapterPool: creating pool of 4 adapters` and `TileLoadScheduler: starting 4
worker threads`.

- [ ] **Step 2: Change to 1**

Set Reading threads = **1**, OK. Open a (different) slide. The log shows
`creating pool of 1 adapters` and `starting 1 worker threads`.

- [ ] **Step 3: Change to 8 + persistence**

Set Reading threads = **8**, Apply. Open another slide → `pool of 8 adapters` / `8 worker
threads`. Reopen Settings → Performance still reads **8**. Close and relaunch the viewer,
reopen Settings → still **8** (persisted).

- [ ] **Step 4: Current slide unaffected**

With a slide open at pool size N, change the value and Apply — the open slide keeps
behaving as before (no log line about a new pool). Only the next open uses the new size.

---

## Notes for the implementer

- **Pattern fidelity:** `ReadingSettings`/`PerformanceTab` mirror `LogSettings`/`LogsTab`
  (module shape, PIMPL, CMake, standalone-compiled unit test linking only `Qt6::Core`).
- **Namespace:** the two pool sites are in `ViewportWidget.cpp`'s anonymous namespace →
  fully-qualified `slideio::viewer::ui::readThreadPoolSize()`. The scheduler site is a ui
  member and uses `m_impl->adapterPool->poolSize()`.
- **No live resize:** the pool is created per open; a changed value applies to the next
  opened slide. This is communicated by the tab's note label.
- **Clamping:** `clampThreadPoolSize` guards read and write; the `QSpinBox` range also
  constrains UI input to `[1, 32]`.
```
