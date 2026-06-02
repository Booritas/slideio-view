# Settings Dialog with Logs Tab — Design

**Date:** 2026-06-02
**Status:** Approved (design)

## Goal

Add a **Tools → Settings…** menu item that opens a modal, tabbed settings dialog.
For this first iteration the dialog has a single tab, **Logs**, that lets the user:

- See the log file location and open the file or its containing folder.
- Choose the application log level (dropdown).
- Turn performance logging on or off (dropdown; default off).

Choices are applied to the running loggers immediately on OK/Apply and persisted
via `QSettings` so they are restored on the next launch.

## Non-Goals

- No additional settings tabs yet (the dialog shell is built to accept more later).
- No log-retention/rotation configuration (rotation stays hardcoded at 10 MB × 3).
- No automated GUI/widget tests (no widget-test harness exists in this repo).

## Architecture

A new `SettingsDialog` (Qt widget, PIMPL idiom per project conventions, modeled on
the existing `OpenS3Dialog`) hosts a `QTabWidget`. Each tab is its own widget so
future tabs can be added without touching the dialog shell. The only tab for now
is `LogsTab`.

Logger configuration logic (mapping dropdown values to `spdlog` levels, reading and
writing `QSettings`, and resolving the startup perf state from the env var + the
persisted setting) lives in a small, Qt-light helper, `LogSettings`, so the dialog
(on Apply) and `main.cpp` (at startup) share one code path. `LogSettings` is the
unit-tested seam.

### New / modified files

| File | Responsibility |
|------|----------------|
| `src/ui/include/slideio/viewer/ui/SettingsDialog.h` (new) | Declares `SettingsDialog : QDialog`, PIMPL. |
| `src/ui/src/SettingsDialog.cpp` (new) | Builds the `QTabWidget` + OK/Cancel/Apply button box; owns the `LogsTab`; wires Apply/OK to persist + apply. |
| `src/ui/include/slideio/viewer/ui/LogsTab.h` (new) | Declares `LogsTab : QWidget` with `load()` / `apply()` (persist + apply to loggers). |
| `src/ui/src/LogsTab.cpp` (new) | Builds the three UI groups; open-file/open-folder button handlers. |
| `src/ui/include/slideio/viewer/ui/LogSettings.h` (new) | Pure helpers: level string ↔ `spdlog::level`, QSettings keys, startup-perf resolution. Qt-light (uses `QSettings`/`QString`). |
| `src/ui/src/LogSettings.cpp` (new) | Implements the helpers. |
| `src/ui/src/MainWindow.cpp` (modify) | Add a `&Tools` menu with a `Settings…` action that opens the dialog modally. |
| `src/main.cpp` (modify) | At startup, apply persisted app level and resolve perf state (env var wins). |
| `src/ui/CMakeLists.txt` (modify) | Add `SettingsDialog.cpp`, `LogsTab.cpp`, `LogSettings.cpp` to the `slideio-viewer-ui` target. |
| `tests/ui/LogSettingsTest.cpp` (new) | Catch2 unit tests for the `LogSettings` pure helpers. |
| `tests/CMakeLists.txt` (modify) | Add a ui test target (or extend an existing one) including `LogSettingsTest.cpp`. |

> Note: the repo currently has `core-tests` and `infra-tests` targets but no `ui`
> test target. The implementation plan must add a `slideio-viewer-ui-tests` target
> (linking `slideio-viewer-ui` + Qt6::Core + Catch2) and register it with CTest.
> If linking the full ui library into a test proves heavy, `LogSettings` may instead
> be placed so it can be tested without pulling in widgets — see Open Questions.

## Logs Tab UI

Vertical layout with three grouped sections, then the dialog's button box.

1. **Log file** group
   - Read-only single-line field showing `slideio::viewer::ui::logFilePath()`.
   - **Open Log File** button — reuses the existing pattern from `MainWindow`
     (`QFile::exists` guard → informational `QMessageBox` if missing →
     `QDesktopServices::openUrl(QUrl::fromLocalFile(path))`).
   - **Open Folder** button — opens `logDirectory()` the same way. The directory
     always exists (created via `QDir().mkpath(...)` in `main.cpp` at startup).

2. **Application log level** group
   - `QComboBox` with items: Trace, Debug, Info, Warning, Error, Off.
   - Maps to `spdlog::level::{trace, debug, info, warn, err, off}`.
   - Default selection: **Debug** (current hardcoded behavior).

3. **Performance logging** group
   - `QComboBox` with two items: **Do not log** (default) / **Log performance data**.
   - "Do not log" → `spdlog::level::off`; "Log performance data" → `spdlog::level::trace`
     (all perf records are emitted at trace level in the current code).

**Buttons:** `QDialogButtonBox` with **OK**, **Cancel**, **Apply**.
The dialog is modal (`exec()`), parented to `MainWindow`.

## Persistence & Apply Semantics

`QSettings` keys (org "SlideIO" / app "SlideIO Viewer" are already set):

| Key | Type | Meaning | Default when absent |
|-----|------|---------|---------------------|
| `logging/appLevel` | string (e.g. `"debug"`) | Application logger level | `"debug"` |
| `logging/perfEnabled` | bool | Performance logging on/off | `false` |

Button behavior:

- **Apply** → write both keys to `QSettings`; apply to live loggers; dialog stays open.
- **OK** → same as Apply, then close (accept).
- **Cancel** → discard pending edits; close (reject). No write, no live change.

"Apply to live loggers":
- `spdlog::get("viewer")->set_level(<mapped app level>)`
- `spdlog::get("perf")->set_level(perfEnabled ? trace : off)`

`spdlog::get(...)` may return null in theory; guard each call (no-op if absent).

## Startup Wiring (main.cpp)

After the loggers are created (current lines ~34–53):

- **Application level:** read `logging/appLevel`. If present and valid, set the
  `viewer` logger to it; otherwise leave the current `debug` default.
- **Performance level — env var wins:**
  - If `SLIDEIO_PERF_LOG` is set and non-empty, behave exactly as today:
    value `"0"` → off; any other non-empty value → on (trace). When on, emit the
    existing `"Performance logging ENABLED (SLIDEIO_PERF_LOG set)"` info line and
    `flush_on(trace)`.
  - Otherwise, apply the persisted `logging/perfEnabled` (on → trace + `flush_on(trace)`,
    off → off).

This keeps scripted/CI runs that set the env var working unchanged, while making the
persisted dropdown the default source of truth for normal launches.

## Error Handling

- **Open Log File** when the file does not exist yet → informational `QMessageBox`
  with the path (same copy as the existing File-menu action).
- **Open buttons** when `QDesktopServices::openUrl` fails → warning `QMessageBox`.
- **Invalid/unknown persisted level string** at startup or load → fall back to the
  default (Debug) rather than crashing.

## Testing

- **`LogSettings` (unit, Catch2):**
  - level string → `spdlog::level` and back is a stable round-trip for all six items.
  - unknown/empty level string maps to the Debug default.
  - startup-perf resolution: env unset/empty → use the persisted bool; env `"0"` → off
    regardless of setting; env non-empty non-`"0"` → on regardless of setting.
- **Dialog / widgets:** manual verification (open Tools → Settings…, change levels,
  confirm log output and persistence across a restart). Consistent with how
  `OpenS3Dialog` is treated (no widget tests).

## Open Questions / Implementer Notes

- **ui test target:** none exists yet. The plan adds `slideio-viewer-ui-tests`. If
  linking the whole ui static lib (which pulls in OpenGL/widgets) into a console test
  is awkward on this toolchain, keep `LogSettings` free of widget dependencies (only
  `QSettings`/`QString` + spdlog) so the test links just what it needs. The
  env+setting resolution helper should take its inputs as parameters (env value as a
  `std::optional<std::string>` or `const char*`, persisted bool as `bool`) so it is
  testable without touching real `QSettings` or `getenv`.
- **Layering:** `LogSettings` lives in **ui** (it uses `QSettings`); both `main.cpp`
  and the dialog already depend on ui, so the include is legal.
