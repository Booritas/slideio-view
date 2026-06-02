# Settings Dialog with Logs Tab Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a **Tools → Settings…** menu item that opens a modal, tabbed dialog whose single **Logs** tab shows the log file location (with open buttons), an application log-level dropdown, and a performance-logging on/off dropdown; choices apply to the live loggers and persist via `QSettings`.

**Architecture:** A `SettingsDialog` (QDialog, PIMPL, modeled on `OpenS3Dialog`) hosts a `QTabWidget` containing a `LogsTab` widget. A Qt-light helper, `LogSettings`, holds the pure mapping/resolution logic plus thin `QSettings`/spdlog glue, shared by the dialog (on Apply) and `main.cpp` (at startup). The pure helpers are the unit-tested seam; widgets are verified manually.

**Tech Stack:** C++17, Qt 6 Widgets, spdlog, QSettings, Catch2 (tests), CMake + Conan.

**Build/test commands** (this machine — `conan` is not on the default PATH):
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build -C Release --output-on-failure
```

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/ui/include/slideio/viewer/ui/LogSettings.h` (new) | Pure helpers (level string ↔ `spdlog::level`, startup-perf resolution) + QSettings/spdlog glue declarations. |
| `src/ui/src/LogSettings.cpp` (new) | Implements the helpers. Qt-light: uses only `QSettings`/`QString` + spdlog. |
| `tests/ui/LogSettingsTest.cpp` (new) | Catch2 unit tests for the pure helpers. |
| `src/ui/include/slideio/viewer/ui/SettingsDialog.h` (new) | `SettingsDialog : QDialog`, PIMPL. |
| `src/ui/src/SettingsDialog.cpp` (new) | Builds the `QTabWidget` + OK/Cancel/Apply; owns the `LogsTab`; wires Apply/OK to `LogsTab::apply()`. |
| `src/ui/include/slideio/viewer/ui/LogsTab.h` (new) | `LogsTab : QWidget`, PIMPL, with `apply()`. |
| `src/ui/src/LogsTab.cpp` (new) | Builds the three UI groups; open-file/open-folder handlers; `apply()` persists + applies. |
| `src/ui/src/MainWindow.cpp` (modify) | Add `settingsAction`, a `&Tools` menu, and the open-dialog connection. |
| `src/main.cpp` (modify) | Apply persisted app level and resolve perf state (env wins) after `QApplication` is constructed. |
| `src/ui/CMakeLists.txt` (modify) | Add the three new `.cpp` files to `slideio-viewer-ui`. |
| `tests/CMakeLists.txt` (modify) | Add a `slideio-viewer-ui-tests` target (compiles `LogSettings.cpp` standalone). |

---

## Task 1: LogSettings helper (pure logic + glue) with unit tests

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/LogSettings.h`
- Create: `src/ui/src/LogSettings.cpp`
- Create: `tests/ui/LogSettingsTest.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/ui/LogSettingsTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/LogSettings.h"

#include <spdlog/common.h>

#include <optional>
#include <string>

using namespace slideio::viewer::ui;

TEST_CASE("level string round-trips for all exposed levels", "[ui][LogSettings]")
{
    const spdlog::level::level_enum levels[] = {
        spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
        spdlog::level::warn,  spdlog::level::err,   spdlog::level::off};
    for (auto lvl : levels) {
        REQUIRE(levelFromString(levelToString(lvl)) == lvl);
    }
}

TEST_CASE("unknown or empty level string defaults to debug", "[ui][LogSettings]")
{
    REQUIRE(levelFromString("") == spdlog::level::debug);
    REQUIRE(levelFromString("bogus") == spdlog::level::debug);
}

TEST_CASE("warning/error aliases parse", "[ui][LogSettings]")
{
    REQUIRE(levelFromString("warning") == spdlog::level::warn);
    REQUIRE(levelFromString("err") == spdlog::level::err);
}

TEST_CASE("startup perf resolution: env wins when set and non-empty", "[ui][LogSettings]")
{
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{"1"}, false) == true);
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{"0"}, true) == false);
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{"yes"}, false) == true);
}

TEST_CASE("startup perf resolution: falls back to setting when env unset/empty", "[ui][LogSettings]")
{
    REQUIRE(resolveStartupPerfEnabled(std::nullopt, true) == true);
    REQUIRE(resolveStartupPerfEnabled(std::nullopt, false) == false);
    REQUIRE(resolveStartupPerfEnabled(std::optional<std::string>{""}, true) == true);
}
```

- [ ] **Step 2: Wire the test target into CMake**

In `tests/CMakeLists.txt`, append after the infra-tests block (after line 27):

```cmake
# UI layer tests — compile LogSettings.cpp standalone (Qt6::Core only, no widgets)
# so the unit test does not pull in OpenGL/Widgets.
add_executable(slideio-viewer-ui-tests
    ui/LogSettingsTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/src/LogSettings.cpp
)

target_include_directories(slideio-viewer-ui-tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ui/include
)

target_link_libraries(slideio-viewer-ui-tests
    PRIVATE Qt6::Core spdlog::spdlog Catch2::Catch2WithMain
)

add_test(NAME ui-tests COMMAND slideio-viewer-ui-tests)
```

- [ ] **Step 3: Run the test to verify it fails to build**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: FAIL — `LogSettings.h` not found / `levelFromString` undefined.

- [ ] **Step 4: Create the header**

Create `src/ui/include/slideio/viewer/ui/LogSettings.h`:

```cpp
#pragma once

#include <spdlog/common.h> // spdlog::level::level_enum

#include <optional>
#include <string>

namespace slideio::viewer::ui
{

// --- Pure helpers (no Qt, unit-tested) ---

// Maps a stored level string to a spdlog level. Accepts "trace", "debug",
// "info", "warn"/"warning", "err"/"error", "off". Unknown/empty -> debug
// (the application's default level).
spdlog::level::level_enum levelFromString(const std::string& text);

// Inverse of levelFromString for the six exposed levels. err -> "error",
// warn -> "warn"; round-trips with levelFromString.
std::string levelToString(spdlog::level::level_enum level);

// Resolves whether performance logging should be on at startup. The
// SLIDEIO_PERF_LOG environment variable wins when present and non-empty
// ("0" -> off, anything else -> on); otherwise the persisted setting is used.
bool resolveStartupPerfEnabled(const std::optional<std::string>& envValue, bool persisted);

// --- QSettings-backed glue (Qt; verified manually) ---

// Reads the persisted application log-level string ("" if never saved).
std::string readAppLevelSetting();

// Reads the persisted performance-logging flag (false if never saved).
bool readPerfEnabledSetting();

// Persists both log settings to QSettings.
void saveLogSettings(spdlog::level::level_enum appLevel, bool perfEnabled);

// --- Live-logger glue (spdlog; verified manually) ---

// Sets the "viewer" (default) logger level. No-op if the logger is absent.
void applyAppLevel(spdlog::level::level_enum level);

// Enables (trace + flush_on trace) or disables (off) the "perf" logger.
// No-op if the logger is absent.
void applyPerfEnabled(bool enabled);

} // namespace slideio::viewer::ui
```

- [ ] **Step 5: Create the implementation**

Create `src/ui/src/LogSettings.cpp`:

```cpp
#include "slideio/viewer/ui/LogSettings.h"

#include <spdlog/spdlog.h>

#include <QSettings>
#include <QString>

namespace
{
const QString kAppLevelKey = "logging/appLevel";
const QString kPerfEnabledKey = "logging/perfEnabled";
} // anonymous namespace

namespace slideio::viewer::ui
{

spdlog::level::level_enum levelFromString(const std::string& text)
{
    if (text == "trace") return spdlog::level::trace;
    if (text == "debug") return spdlog::level::debug;
    if (text == "info") return spdlog::level::info;
    if (text == "warn" || text == "warning") return spdlog::level::warn;
    if (text == "err" || text == "error") return spdlog::level::err;
    if (text == "off") return spdlog::level::off;
    return spdlog::level::debug; // default
}

std::string levelToString(spdlog::level::level_enum level)
{
    switch (level) {
    case spdlog::level::trace: return "trace";
    case spdlog::level::debug: return "debug";
    case spdlog::level::info: return "info";
    case spdlog::level::warn: return "warn";
    case spdlog::level::err: return "error";
    case spdlog::level::off: return "off";
    default: return "debug";
    }
}

bool resolveStartupPerfEnabled(const std::optional<std::string>& envValue, bool persisted)
{
    if (envValue.has_value() && !envValue->empty()) {
        return *envValue != "0";
    }
    return persisted;
}

std::string readAppLevelSetting()
{
    QSettings settings;
    return settings.value(kAppLevelKey).toString().toStdString();
}

bool readPerfEnabledSetting()
{
    QSettings settings;
    return settings.value(kPerfEnabledKey, false).toBool();
}

void saveLogSettings(spdlog::level::level_enum appLevel, bool perfEnabled)
{
    QSettings settings;
    settings.setValue(kAppLevelKey, QString::fromStdString(levelToString(appLevel)));
    settings.setValue(kPerfEnabledKey, perfEnabled);
}

void applyAppLevel(spdlog::level::level_enum level)
{
    if (auto viewer = spdlog::get("viewer")) {
        viewer->set_level(level);
    }
}

void applyPerfEnabled(bool enabled)
{
    if (auto perf = spdlog::get("perf")) {
        perf->set_level(enabled ? spdlog::level::trace : spdlog::level::off);
        if (enabled) {
            perf->flush_on(spdlog::level::trace);
        }
    }
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 6: Add the source to the ui library**

In `src/ui/CMakeLists.txt`, add `src/LogSettings.cpp` to the `slideio-viewer-ui` source list (after `src/AppPaths.cpp`, line 2):

```cmake
add_library(slideio-viewer-ui STATIC
    src/AppPaths.cpp
    src/LogSettings.cpp
    src/AssociatedImageWindow.cpp
```

- [ ] **Step 7: Build and run the tests to verify they pass**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
ctest --test-dir build/build -C Release -R ui-tests --output-on-failure
```
Expected: PASS — all five `LogSettings` test cases pass.

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/LogSettings.h src/ui/src/LogSettings.cpp \
        tests/ui/LogSettingsTest.cpp src/ui/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add LogSettings helper (level mapping, perf resolution, QSettings glue)"
```

---

## Task 2: LogsTab and SettingsDialog widgets

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/LogsTab.h`
- Create: `src/ui/src/LogsTab.cpp`
- Create: `src/ui/include/slideio/viewer/ui/SettingsDialog.h`
- Create: `src/ui/src/SettingsDialog.cpp`
- Modify: `src/ui/CMakeLists.txt`

No unit test: these are Qt widgets with no test harness in this repo (consistent with `OpenS3Dialog`). Verified by build + the manual run in Task 5.

- [ ] **Step 1: Create the LogsTab header**

Create `src/ui/include/slideio/viewer/ui/LogsTab.h`:

```cpp
#pragma once

#include <QWidget>

#include <memory>

namespace slideio::viewer::ui
{

// The "Logs" tab of the Settings dialog: shows the log file location (with
// open buttons), an application log-level dropdown, and a performance-logging
// on/off dropdown. Selections are loaded from QSettings on construction and
// written back (and applied to the live loggers) by apply().
class LogsTab : public QWidget
{
    Q_OBJECT

public:
    explicit LogsTab(QWidget* parent = nullptr);
    ~LogsTab() override;

    LogsTab(const LogsTab&) = delete;
    LogsTab& operator=(const LogsTab&) = delete;

    // Persist the current selections to QSettings and apply them to the live
    // "viewer" and "perf" loggers.
    void apply();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
```

- [ ] **Step 2: Create the LogsTab implementation**

Create `src/ui/src/LogsTab.cpp`:

```cpp
#include "slideio/viewer/ui/LogsTab.h"

#include "slideio/viewer/ui/AppPaths.h"
#include "slideio/viewer/ui/LogSettings.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct LogsTab::Impl
{
    QLineEdit* pathEdit = nullptr;
    QComboBox* appLevelCombo = nullptr;
    QComboBox* perfCombo = nullptr;
};

LogsTab::LogsTab(QWidget* parent)
    : QWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    // --- Log file group ---
    m_impl->pathEdit = new QLineEdit(this);
    m_impl->pathEdit->setReadOnly(true);
    m_impl->pathEdit->setText(logFilePath());

    auto* openFileButton = new QPushButton("Open Log File", this);
    auto* openFolderButton = new QPushButton("Open Folder", this);

    auto* fileButtons = new QHBoxLayout;
    fileButtons->addWidget(openFileButton);
    fileButtons->addWidget(openFolderButton);
    fileButtons->addStretch();

    auto* fileLayout = new QVBoxLayout;
    fileLayout->addWidget(m_impl->pathEdit);
    fileLayout->addLayout(fileButtons);

    auto* fileGroup = new QGroupBox("Log file", this);
    fileGroup->setLayout(fileLayout);

    // --- Levels group ---
    m_impl->appLevelCombo = new QComboBox(this);
    m_impl->appLevelCombo->addItem("Trace", "trace");
    m_impl->appLevelCombo->addItem("Debug", "debug");
    m_impl->appLevelCombo->addItem("Info", "info");
    m_impl->appLevelCombo->addItem("Warning", "warn");
    m_impl->appLevelCombo->addItem("Error", "error");
    m_impl->appLevelCombo->addItem("Off", "off");

    m_impl->perfCombo = new QComboBox(this);
    m_impl->perfCombo->addItem("Do not log", false);
    m_impl->perfCombo->addItem("Log performance data", true);

    auto* levelsForm = new QFormLayout;
    levelsForm->addRow("Application log level:", m_impl->appLevelCombo);
    levelsForm->addRow("Performance logging:", m_impl->perfCombo);

    auto* levelsGroup = new QGroupBox("Logging levels", this);
    levelsGroup->setLayout(levelsForm);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(fileGroup);
    layout->addWidget(levelsGroup);
    layout->addStretch();

    // --- Load current selections from QSettings ---
    const std::string appLevel = readAppLevelSetting();
    const QString appLevelData =
        QString::fromStdString(appLevel.empty() ? std::string("debug") : appLevel);
    int appIdx = m_impl->appLevelCombo->findData(appLevelData);
    m_impl->appLevelCombo->setCurrentIndex(appIdx >= 0 ? appIdx : 1 /* Debug */);

    int perfIdx = m_impl->perfCombo->findData(readPerfEnabledSetting());
    m_impl->perfCombo->setCurrentIndex(perfIdx >= 0 ? perfIdx : 0 /* Do not log */);

    // --- Open buttons (reuse the MainWindow open-log pattern) ---
    connect(openFileButton, &QPushButton::clicked, this, [this]() {
        const QString path = logFilePath();
        if (!QFile::exists(path)) {
            QMessageBox::information(this, "Open Log File",
                QString("Log file does not exist yet:\n%1").arg(path));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            QMessageBox::warning(this, "Open Log File",
                QString("Failed to open log file:\n%1").arg(path));
        }
    });

    connect(openFolderButton, &QPushButton::clicked, this, [this]() {
        const QString dir = logDirectory();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
            QMessageBox::warning(this, "Open Folder",
                QString("Failed to open log folder:\n%1").arg(dir));
        }
    });
}

LogsTab::~LogsTab() = default;

void LogsTab::apply()
{
    const spdlog::level::level_enum level =
        levelFromString(m_impl->appLevelCombo->currentData().toString().toStdString());
    const bool perfEnabled = m_impl->perfCombo->currentData().toBool();

    saveLogSettings(level, perfEnabled);
    applyAppLevel(level);
    applyPerfEnabled(perfEnabled);
}

} // namespace slideio::viewer::ui
```

> Note: `LogSettings.h` includes `<spdlog/common.h>`, so `spdlog::level::level_enum` is available in `apply()` without an extra include.

- [ ] **Step 3: Create the SettingsDialog header**

Create `src/ui/include/slideio/viewer/ui/SettingsDialog.h`:

```cpp
#pragma once

#include <QDialog>

#include <memory>

namespace slideio::viewer::ui
{

// Modal, tabbed application-settings dialog. Currently exposes a single "Logs"
// tab. OK/Apply persist each tab's settings and apply them live; Cancel
// discards. Built to accept additional tabs without changing the shell.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

    SettingsDialog(const SettingsDialog&) = delete;
    SettingsDialog& operator=(const SettingsDialog&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
```

- [ ] **Step 4: Create the SettingsDialog implementation**

Create `src/ui/src/SettingsDialog.cpp`:

```cpp
#include "slideio/viewer/ui/SettingsDialog.h"

#include "slideio/viewer/ui/LogsTab.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct SettingsDialog::Impl
{
    QTabWidget* tabs = nullptr;
    LogsTab* logsTab = nullptr;
};

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle("Settings");
    setModal(true);

    m_impl->logsTab = new LogsTab(this);
    m_impl->tabs = new QTabWidget(this);
    m_impl->tabs->addTab(m_impl->logsTab, "Logs");

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_impl->tabs);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        m_impl->logsTab->apply();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        m_impl->logsTab->apply();
    });
}

SettingsDialog::~SettingsDialog() = default;

} // namespace slideio::viewer::ui
```

- [ ] **Step 5: Add the sources to the ui library**

In `src/ui/CMakeLists.txt`, add the two new `.cpp` files to the `slideio-viewer-ui` source list (after the `src/OpenS3Dialog.cpp` line, line 18):

```cmake
    src/OpenS3Dialog.cpp
    src/SettingsDialog.cpp
    src/LogsTab.cpp
    src/MainWindow.cpp
```

- [ ] **Step 6: Build to verify it compiles**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: build succeeds (AUTOMOC picks up the new `Q_OBJECT` headers via the existing `include/*.h` glob; the widgets are compiled but not yet used).

- [ ] **Step 7: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/LogsTab.h src/ui/src/LogsTab.cpp \
        src/ui/include/slideio/viewer/ui/SettingsDialog.h src/ui/src/SettingsDialog.cpp \
        src/ui/CMakeLists.txt
git commit -m "Add SettingsDialog with a Logs tab"
```

---

## Task 3: Tools → Settings… menu item in MainWindow

**Files:**
- Modify: `src/ui/src/MainWindow.cpp` (include at top; `Impl` actions ~line 102; `createActions()` ~line 121; `createMenus()` ~line 181; `connectSignals()` ~line 231)

No unit test: menu wiring, verified by the manual run in Task 5.

- [ ] **Step 1: Add the SettingsDialog include**

In `src/ui/src/MainWindow.cpp`, add after the `OpenS3Dialog.h` include (line 11):

```cpp
#include "slideio/viewer/ui/OpenS3Dialog.h"
#include "slideio/viewer/ui/SettingsDialog.h"
```

- [ ] **Step 2: Declare the action**

In the `Impl` struct's action list, add after `metadataToggleAction` (line 102):

```cpp
    QAction* metadataToggleAction = nullptr;
    QAction* settingsAction = nullptr;
```

- [ ] **Step 3: Create the action**

In `createActions()`, add after the `exitAction` block (after line 123, `exitAction->setStatusTip(...)`):

```cpp
        settingsAction = new QAction("&Settings...", owner);
        settingsAction->setStatusTip("Configure application settings");
```

- [ ] **Step 4: Add the Tools menu**

In `createMenus()`, add after the `viewMenu` block closes (after line 181, `viewMenu->addAction(metadataToggleAction);`):

```cpp
        viewMenu->addAction(metadataToggleAction);

        QMenu* toolsMenu = owner->menuBar()->addMenu("&Tools");
        toolsMenu->addAction(settingsAction);
    }
```

> Replace the existing closing `}` of `createMenus()` — do not add a second one.

- [ ] **Step 5: Connect the action**

In `connectSignals()`, add after the `exitAction` connection (after line 231):

```cpp
        QObject::connect(exitAction, &QAction::triggered, owner, &QMainWindow::close);

        QObject::connect(settingsAction, &QAction::triggered, owner, [this]() {
            SettingsDialog dialog(owner);
            dialog.exec();
        });
```

- [ ] **Step 6: Build to verify it compiles**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: build succeeds.

- [ ] **Step 7: Commit**

```bash
git add src/ui/src/MainWindow.cpp
git commit -m "Add Tools menu with Settings... action"
```

---

## Task 4: Apply persisted log settings at startup

**Files:**
- Modify: `src/main.cpp` (perf-logger block ~lines 40-53; add include; add a settings-apply block after `QApplication app(argc, argv);` ~line 64)

No unit test: process bootstrap. The pure resolver is already tested in Task 1; verified end-to-end by the manual run in Task 5.

- [ ] **Step 1: Add the LogSettings include**

In `src/main.cpp`, add after the `MainWindow.h` include (line 13):

```cpp
#include "slideio/viewer/ui/MainWindow.h"
#include "slideio/viewer/ui/LogSettings.h"
```

Also add `<optional>` with the other standard includes (after `<string>`, line 16):

```cpp
#include <cstdlib>
#include <optional>
#include <string>
```

- [ ] **Step 2: Simplify the perf-logger registration**

In `src/main.cpp`, replace the perf-logger block (current lines 40-53, from the `// Dedicated perf logger` comment through the closing `}` of the `if (perfOn)` block) with:

```cpp
    // Dedicated perf logger for tile-loading / rendering timing. Shares the
    // main logger's sinks but is OFF by default. Its level is decided below,
    // once QApplication exists, from SLIDEIO_PERF_LOG (env wins) or the
    // persisted setting (Tools -> Settings -> Logs).
    auto perfLogger = std::make_shared<spdlog::logger>("perf",
        spdlog::sinks_init_list{fileSink, consoleSink});
    perfLogger->set_level(spdlog::level::off);
    spdlog::register_logger(perfLogger);
```

- [ ] **Step 3: Apply persisted settings after QApplication is constructed**

In `src/main.cpp`, immediately after `QApplication app(argc, argv);` (line 64), insert:

```cpp
    QApplication app(argc, argv);

    // Apply persisted log settings now that QSettings (org/app identity set
    // above) is safe to use. App level: persisted value, else the debug default.
    // Perf level: SLIDEIO_PERF_LOG wins when set; otherwise the persisted flag.
    {
        using namespace slideio::viewer::ui;
        applyAppLevel(levelFromString(readAppLevelSetting()));

        const char* perfEnv = std::getenv("SLIDEIO_PERF_LOG");
        std::optional<std::string> perfEnvOpt;
        if (perfEnv) {
            perfEnvOpt = std::string(perfEnv);
        }
        const bool perfEnabled = resolveStartupPerfEnabled(perfEnvOpt, readPerfEnabledSetting());
        applyPerfEnabled(perfEnabled);
        if (perfEnabled) {
            spdlog::info("Performance logging ENABLED");
        }
    }
```

> `readAppLevelSetting()` returns "" when never saved, and `levelFromString("")` is the debug default — so this preserves current behavior on a fresh install.

- [ ] **Step 4: Build to verify it compiles**

Run:
```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
```
Expected: build succeeds.

- [ ] **Step 5: Run all tests**

Run:
```bash
ctest --test-dir build/build -C Release --output-on-failure
```
Expected: all tests PASS (`core-tests`, `infra-tests`, `ui-tests`).

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "Apply persisted log settings at startup (env var wins for perf)"
```

---

## Task 5: Manual verification

**Files:** none (runtime check). Use the installed build at
`build/install/release/bin/slideio-viewer.exe` (DLLs deployed). Log file:
`%LOCALAPPDATA%\SlideIO\SlideIO Viewer\logs\slideio-viewer.log`.

- [ ] **Step 1: Menu + dialog open**

Launch the viewer (no env var). **Tools → Settings…** opens a modal dialog titled "Settings" with a single "Logs" tab showing: the log file path (read-only), Open Log File / Open Folder buttons, an "Application log level" dropdown (default **Debug**), and a "Performance logging" dropdown (default **Do not log**).

- [ ] **Step 2: Open buttons**

Click **Open Log File** → the log opens in the default text editor (or an info box if it doesn't exist yet). Click **Open Folder** → the logs directory opens in the file manager.

- [ ] **Step 3: App level applies + persists**

Set Application log level to **Trace**, click **OK**. Pan/zoom and confirm the log now shows trace-level lines. Close and relaunch the viewer; reopen Settings → the dropdown still reads **Trace**, and trace lines still appear (persistence works).

- [ ] **Step 4: Perf toggle applies + persists**

With `SLIDEIO_PERF_LOG` unset, set Performance logging to **Log performance data**, click **Apply**. Confirm "Performance logging ENABLED" and `requestVisibleTiles` / `readTile` / `viewport refined` lines appear after a pan/zoom. Relaunch → perf lines still appear (persisted). Set back to **Do not log**, OK, relaunch → no perf lines.

- [ ] **Step 5: Env var still wins**

Set `SLIDEIO_PERF_LOG=1` and launch with the dropdown saved as **Do not log** → perf lines appear anyway (env overrides). Set `SLIDEIO_PERF_LOG=0` with the dropdown saved as **Log performance data** → no perf lines (env overrides). Unset the var afterward.

- [ ] **Step 6: Cancel discards**

Open Settings, change a dropdown, click **Cancel**. Reopen Settings → the dropdown shows the previously-saved value (no change persisted or applied).

---

## Notes for the implementer

- **Apply vs save:** `LogsTab::apply()` is the single place that both persists (`saveLogSettings`) and applies to live loggers (`applyAppLevel`/`applyPerfEnabled`). OK calls it then `accept()`; Apply calls it and stays open; Cancel does neither.
- **Logger names:** the default/main logger is registered as `"viewer"` and the perf logger as `"perf"` in `main.cpp`. `applyAppLevel` targets `"viewer"`; `applyPerfEnabled` targets `"perf"`. Both no-op if `spdlog::get(...)` returns null.
- **Why the startup apply moved after `QApplication`:** keeps `QSettings` usage unambiguously after the org/app identity is set and an event-loop-capable app exists, avoiding any platform quibbles about `QSettings` before `QApplication`.
- **Test isolation:** `ui-tests` compiles `LogSettings.cpp` directly (not the whole ui lib) and links only `Qt6::Core` + spdlog, so it stays light and the pure functions need no `QApplication`.
- **`{}` format / spdlog levels:** `spdlog::level::err` is the value for "Error" (string `"error"`); `warn` ↔ `"warn"`. The round-trip test guards this.
```
