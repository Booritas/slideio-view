# CI and Binary Distribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the viewer a per-commit CI gate and a tag-driven release pipeline that produces a working NSIS installer, macOS arm64 disk image and Debian package.

**Architecture:** Every platform-specific packaging rule lives in CMake or in a script a developer can run (`package.sh`, `scripts/smoke-package.*`); the two workflow files only sequence those steps, cache, and upload. The hardcoded Qt path that this replaces survived precisely because nothing but `cmake --install` exercised it.

**Tech Stack:** CMake 3.20+ / CPack (NSIS, DragNDrop, DEB, TGZ, ZIP), Conan 2, Qt 6.7.3, bash (Git Bash on Windows), PowerShell, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-03-ci-distribution-design.md`

## Global Constraints

- Runners are pinned, never `-latest`: `windows-2022`, `ubuntu-22.04`, `macos-14`.
- `ubuntu-22.04` is load-bearing: glibc 2.35 floor, so the `.deb` installs on Ubuntu 22.04+ and Debian 12+. Do not "upgrade" it.
- Release configuration only. No debug distribution.
- The environment variable `build-template.sh` reads is spelled `CONAN_PROFIlE` — lowercase `l`. Spell it that way everywhere; fixing the typo is a separate change.
- Artifacts are named `slideio-viewer-<version>-{windows-x86_64,macos-arm64,linux-x86_64}`; the `.deb` keeps the Debian-native name `DEB-DEFAULT` produces.
- The version has exactly one source: `project(slideio-viewer VERSION x.y.z ...)` at `CMakeLists.txt:2`.
- No code signing or notarisation. No GUI automation.
- Unix build tree is `build/build/Release`; Windows (multi-config) is `build/build`.
- `CMAKE_INSTALL_PREFIX` is set with `FORCE` at `CMakeLists.txt:142`, so `-DCMAKE_INSTALL_PREFIX=` does not stick. Override at install time with `cmake --install --prefix`, or let CPack use its own staging prefix.

## Review Focus

Five failure modes the spec implies that no obvious task would exercise. Each has a test assigned to the task that owns the code.

1. **A prerelease tag.** `v0.2.0-rc1` against `project(... VERSION 0.2.0)`: a naive compare rejects a legitimate release candidate, and a naive strip accepts `v0.2.0-rc1` for a `0.3.0` tree. Expected: the tag's version core must equal the project version, and a suffix is allowed. Test in **Task 8**.
2. **A stale `build/packages`.** A second `package.sh` run, or a run after the generator list changed, leaves the previous run's artifacts behind and `upload-artifact` ships both. Expected: the directory holds only what this run produced. Test in **Task 3**.
3. **A partially restored SlideIO cache.** `build-template.sh` skips the submodule build when one header exists; a truncated or cancelled cache save leaves that header without the libraries, and the viewer then links against a broken install. Expected: an incomplete tree is discarded and rebuilt. Test in **Task 7**.
4. **System Qt present alongside the bundled Qt.** On a Debian box that already has Qt 6 installed, the app must load the plugins from `/opt/slideio-viewer/plugins`, not the system's. Expected: it starts correctly anyway. Test in **Task 5**, run in **Task 8**.
5. **A deployment tool that succeeds but copies the wrong configuration.** `windeployqt` exits 0 having staged `Qt6Cored.dll` for a Release install, or no platform plugin at all. Expected: the install fails loudly. Test in **Task 2**.

---

### Task 1: Install layout option

**Files:**
- Modify: `CMakeLists.txt:146-156` (install layout variables and `install(TARGETS)`)
- Modify: `CMakeLists.txt:241-248` (the `qt.conf` writer)
- Modify: `cmake/DeployMacOSBundle.cmake:15-16`
- Modify: `build-template.sh:95-100` (pass the layout explicitly)

**Interfaces:**
- Consumes: nothing.
- Produces: cache variable `SLIDEIO_VIEWER_INSTALL_LAYOUT` (`per-config` | `flat`); CMake variables `_install_bin`, `_install_lib`, `_install_plugins`, `_install_app`; install-time variables `SLIDEIO_VIEWER_BIN_DIR`, `SLIDEIO_VIEWER_PLUGIN_DIR`, `SLIDEIO_VIEWER_APP_DIR`. Tasks 2, 3, 5 and 6 all use these.

- [ ] **Step 1: Write the failing check**

There is no unit-test framework for CMake here, so the check is a configure-and-install assertion. Create `scripts/check-install-layout.sh`:

```bash
#!/usr/bin/env bash
# Asserts that an install tree has the shape the requested layout promises.
# Usage: check-install-layout.sh <prefix> <per-config|flat>
set -euo pipefail

prefix="$1"
layout="$2"

case "$layout" in
    per-config) bin="$prefix/release/bin" ;;
    flat)       bin="$prefix/bin" ;;
    *) echo "unknown layout: $layout" >&2; exit 2 ;;
esac

exe="$bin/slideio-viewer"
[ -f "$exe" ] || exe="$exe.exe"
if [ ! -f "$exe" ] && [ ! -d "$bin/slideio-viewer.app" ] && [ ! -d "$prefix/slideio-viewer.app" ]; then
    echo "FAIL: no slideio-viewer under $bin (layout=$layout)" >&2
    find "$prefix" -maxdepth 3 >&2
    exit 1
fi
echo "OK: $layout layout present under $prefix"
```

- [ ] **Step 2: Run it to verify it fails**

```bash
chmod +x scripts/check-install-layout.sh
cmake --install build/build --config Release --prefix build/check-flat
./scripts/check-install-layout.sh build/check-flat flat
```

Expected: FAIL — today's install always writes `release/bin`, so the `flat` assertion cannot pass.

- [ ] **Step 3: Add the layout option**

Replace `CMakeLists.txt:146-150` (the `# Per-config install layout` comment and the two `set(_install_*)` lines) with:

```cmake
# Install tree shape. The developer workflow wants build/install/release/bin and
# build/install/debug/bin side by side; a package wants bin/ at the prefix root,
# or the .deb ends up carrying /opt/slideio-viewer/release/bin/slideio-viewer.
#
# build.sh and package.sh each pass this explicitly, so a cached value from one
# can never leak into the other -- an option() default would not, since it only
# applies when the cache is empty.
set(SLIDEIO_VIEWER_INSTALL_LAYOUT "per-config" CACHE STRING
    "Install tree shape: per-config (release/bin, debug/bin) or flat (bin)")
set_property(CACHE SLIDEIO_VIEWER_INSTALL_LAYOUT PROPERTY STRINGS per-config flat)

if(SLIDEIO_VIEWER_INSTALL_LAYOUT STREQUAL "flat")
    set(_install_subdir "")
elseif(SLIDEIO_VIEWER_INSTALL_LAYOUT STREQUAL "per-config")
    # Generator expressions are evaluated at install time, so this works for both
    # single-config (Make/Ninja) and multi-config (Visual Studio) generators.
    set(_install_subdir "$<LOWER_CASE:$<CONFIG>>/")
else()
    message(FATAL_ERROR
        "SLIDEIO_VIEWER_INSTALL_LAYOUT must be per-config or flat, "
        "got '${SLIDEIO_VIEWER_INSTALL_LAYOUT}'")
endif()

set(_install_bin     "${_install_subdir}bin")
set(_install_lib     "${_install_subdir}lib")
set(_install_plugins "${_install_subdir}plugins")

# The DragNDrop generator wants slideio-viewer.app at the root of the disk image,
# not inside a bin/ directory the user would have to open first.
if(APPLE AND SLIDEIO_VIEWER_INSTALL_LAYOUT STREQUAL "flat")
    set(_install_app ".")
else()
    set(_install_app "${_install_bin}")
endif()
```

- [ ] **Step 4: Point the bundle destination at `_install_app`**

In the `install(TARGETS slideio-viewer ...)` block at `CMakeLists.txt:152`:

```cmake
install(TARGETS slideio-viewer
    RUNTIME DESTINATION ${_install_bin}
    BUNDLE DESTINATION ${_install_app}
)
```

- [ ] **Step 5: Make the `qt.conf` writer and the macOS deploy script layout-aware**

Replace the `install(CODE [[ ... ]])` block at `CMakeLists.txt:241-248` with:

```cmake
if(_qt_release_bin_dir OR _qt_debug_bin_dir)
    # qt.conf tells Qt where to find plugins (../plugins, relative to bin/).
    # The directories are injected rather than recomputed here, so the layout
    # option stays the single place that decides the tree shape.
    install(CODE "set(SLIDEIO_VIEWER_BIN_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_bin}\")")
    install(CODE [[
        file(WRITE "${SLIDEIO_VIEWER_BIN_DIR}/qt.conf" "[Paths]\nPlugins=../plugins\n")
    ]])
endif()
```

In the `if(APPLE)` block at `CMakeLists.txt:252`, add the app directory alongside the two variables already injected:

```cmake
if(APPLE)
    install(CODE "set(MACDEPLOYQT_EXECUTABLE \"${MACDEPLOYQT_EXECUTABLE}\")")
    install(CODE "set(SLIDEIO_ROOT \"${SLIDEIO_ROOT}\")")
    install(CODE "set(SLIDEIO_VIEWER_APP_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_app}\")")
    install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployMacOSBundle.cmake")
endif()
```

And in `cmake/DeployMacOSBundle.cmake`, replace lines 15-16:

```cmake
set(_bundle "${SLIDEIO_VIEWER_APP_DIR}/slideio-viewer.app")
```

Leave `_cfg` and `_slideio_src_dir` alone below it: those index SlideIO's own install tree, which has its own per-config layout unrelated to ours.

- [ ] **Step 6: Have `build-template.sh` pass the layout explicitly**

In `build-template.sh`, add `-DSLIDEIO_VIEWER_INSTALL_LAYOUT="${INSTALL_LAYOUT:-per-config}"` to the `cmake -S . -B` invocation:

```bash
cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
    -DSLIDEIO_VIEWER_INSTALL_LAYOUT="${INSTALL_LAYOUT:-per-config}" \
    -DSLIDEIO_ROOT="$SLIDEIO_ROOT"
```

- [ ] **Step 7: Run both checks to verify they pass**

```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./build.sh
rm -rf build/check-per-config && cmake --install build/build --config Release --prefix build/check-per-config
./scripts/check-install-layout.sh build/check-per-config per-config

INSTALL_LAYOUT=flat ./build.sh
rm -rf build/check-flat && cmake --install build/build --config Release --prefix build/check-flat
./scripts/check-install-layout.sh build/check-flat flat
```

Expected: both print `OK:`. Then restore the default build with `./build.sh`.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt cmake/DeployMacOSBundle.cmake build-template.sh scripts/check-install-layout.sh
git commit -m "Let the install tree be flat as well as per-config"
```

---

### Task 2: Qt discovery and Windows deployment

**Files:**
- Create: `cmake/DeployQt.cmake`
- Create: `cmake/DeployWindowsQt.cmake`
- Modify: `CMakeLists.txt:172-240` (delete `_find_qt_bin_dir` and both `install` blocks that use it)

**Interfaces:**
- Consumes: `_install_bin`, `_install_plugins` from Task 1.
- Produces: CMake function `slideio_viewer_qt_bin_dir(<out_var>)`, returning the directory holding the Qt tools and (on Windows) the Qt DLLs. Tasks 5 and 6 call it.

- [ ] **Step 1: Write the failing check**

Create `scripts/check-windows-qt-deploy.ps1`:

```powershell
# Asserts that a Windows install tree carries the Qt runtime the application
# needs to reach its event loop. This is the regression test for the OPTIONAL
# installs that let an empty installer build cleanly.
param([Parameter(Mandatory=$true)][string]$Prefix,
      [ValidateSet('Release','Debug')][string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
$suffix = if ($Config -eq 'Debug') { 'd' } else { '' }
$required = @(
    "bin\Qt6Core$suffix.dll",
    "bin\Qt6Gui$suffix.dll",
    "bin\Qt6Widgets$suffix.dll",
    "bin\Qt6OpenGLWidgets$suffix.dll",
    "bin\qt.conf",
    "plugins\platforms\qwindows$suffix.dll"
)
$missing = $required | Where-Object { -not (Test-Path (Join-Path $Prefix $_)) }
if ($missing) {
    Write-Host "FAIL: missing from $Prefix :"
    $missing | ForEach-Object { Write-Host "  $_" }
    exit 1
}
# A Release tree carrying debug Qt (or the reverse) loads nothing at runtime.
$wrong = if ($Config -eq 'Release') { 'bin\Qt6Cored.dll' } else { 'bin\Qt6Core.dll' }
if (Test-Path (Join-Path $Prefix $wrong)) {
    Write-Host "FAIL: $Config tree also contains $wrong"
    exit 1
}
Write-Host "OK: Qt runtime present in $Prefix ($Config)"
```

- [ ] **Step 2: Run it to verify it fails**

```powershell
cmake --install build/build --config Release --prefix build/check-flat
powershell -File scripts/check-windows-qt-deploy.ps1 -Prefix build/check-flat -Config Release
```

Expected: FAIL listing `plugins\platforms\qwindows.dll` at minimum. (On the machine whose Conan cache is at `d:/conan2` the DLLs may be present via the old glob; the platform plugin and `qt.conf` placement are what fail.)

- [ ] **Step 3: Write the Qt discovery helper**

Create `cmake/DeployQt.cmake`:

```cmake
# Locates the Qt runtime for deployment.
#
# The previous implementation globbed "d:/conan2/p/b/qt*/p/bin", a path that
# exists on one developer machine. Qt6::qmake is an imported target CMakeDeps
# generates, so its location resolves wherever the Conan cache actually lives --
# and on a CI runner, which is the case the glob could never serve.

function(slideio_viewer_qt_bin_dir out_var)
    if(NOT TARGET Qt6::qmake)
        message(FATAL_ERROR
            "Qt6::qmake was not defined by find_package(Qt6); cannot locate the "
            "Qt runtime to deploy.")
    endif()
    get_target_property(_qmake_loc Qt6::qmake IMPORTED_LOCATION)
    if(NOT _qmake_loc)
        message(FATAL_ERROR "Qt6::qmake has no IMPORTED_LOCATION; cannot locate the Qt runtime.")
    endif()
    get_filename_component(_bin_dir "${_qmake_loc}" DIRECTORY)
    set(${out_var} "${_bin_dir}" PARENT_SCOPE)
endfunction()
```

- [ ] **Step 4: Write the Windows deployment script**

Create `cmake/DeployWindowsQt.cmake`:

```cmake
# Runs windeployqt against the installed executable and then checks its work.
#
# Invoked from CMakeLists.txt via install(SCRIPT ...). Expects these variables,
# set by surrounding install(CODE) blocks:
#   WINDEPLOYQT_EXECUTABLE     - path to windeployqt.exe
#   SLIDEIO_VIEWER_BIN_DIR     - installed bin directory
#   SLIDEIO_VIEWER_PLUGIN_DIR  - installed plugins directory

set(_exe "${SLIDEIO_VIEWER_BIN_DIR}/slideio-viewer.exe")
if(NOT EXISTS "${_exe}")
    message(FATAL_ERROR "DeployWindowsQt: executable not found at '${_exe}'")
endif()

if("${CMAKE_INSTALL_CONFIG_NAME}" STREQUAL "Debug")
    set(_config_flag "--debug")
else()
    set(_config_flag "--release")
endif()

message(STATUS "windeployqt: ${WINDEPLOYQT_EXECUTABLE} ${_exe}")
execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            ${_config_flag}
            --dir "${SLIDEIO_VIEWER_BIN_DIR}"
            --plugindir "${SLIDEIO_VIEWER_PLUGIN_DIR}"
            --no-translations
            --no-compiler-runtime
            "${_exe}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (exit ${_rc})")
endif()

# windeployqt can exit 0 having staged nothing useful -- a wrong --debug/--release
# pairing is the common way. An installer that builds cleanly and contains no Qt
# is the exact failure this packaging work exists to remove, so assert rather
# than trust the exit code.
if("${CMAKE_INSTALL_CONFIG_NAME}" STREQUAL "Debug")
    set(_suffix "d")
else()
    set(_suffix "")
endif()
foreach(_required
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Core${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Gui${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6Widgets${_suffix}.dll"
        "${SLIDEIO_VIEWER_BIN_DIR}/Qt6OpenGLWidgets${_suffix}.dll"
        "${SLIDEIO_VIEWER_PLUGIN_DIR}/platforms/qwindows${_suffix}.dll")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "windeployqt did not produce '${_required}'")
    endif()
endforeach()
message(STATUS "Qt runtime deployed to ${SLIDEIO_VIEWER_BIN_DIR}")
```

- [ ] **Step 5: Replace the glob in CMakeLists.txt**

Delete everything from `# Qt runtime deployment — copy DLLs and plugins per config.` (`CMakeLists.txt:172`) through the end of the `if(_qt_debug_bin_dir)` block (`CMakeLists.txt:240`) — the `_find_qt_bin_dir` function, both `_find_qt_bin_dir` calls, `_qt_modules`, and both per-config `install` blocks. Replace with:

```cmake
# Qt runtime deployment. windeployqt resolves the module set, the platform
# plugin and the style plugins from the binary itself, which does not drift the
# way a hand-maintained module list does.
include(DeployQt)
slideio_viewer_qt_bin_dir(_qt_bin_dir)
message(STATUS "Qt bin dir: ${_qt_bin_dir}")

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        PATHS "${_qt_bin_dir}"
        NO_DEFAULT_PATH)
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR
            "windeployqt was not found in '${_qt_bin_dir}'. The Windows package "
            "cannot be built without it.")
    endif()
    install(CODE "set(WINDEPLOYQT_EXECUTABLE \"${WINDEPLOYQT_EXECUTABLE}\")")
    install(CODE "set(SLIDEIO_VIEWER_BIN_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_bin}\")")
    install(CODE "set(SLIDEIO_VIEWER_PLUGIN_DIR \"\${CMAKE_INSTALL_PREFIX}/${_install_plugins}\")")
    install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindowsQt.cmake")
endif()
```

Then change the `qt.conf` guard from Task 1 — `if(_qt_release_bin_dir OR _qt_debug_bin_dir)` — to `if(WIN32 OR UNIX AND NOT APPLE)`, since those two variables no longer exist. macOS gets its `qt.conf` from `macdeployqt`.

Keep the `qt.conf` block *after* the `install(SCRIPT ...)` above: `windeployqt` writes its own `qt.conf` when it relocates plugins, and ours must be the one that survives.

**Note:** the `tests/CMakeLists.txt` PATH plumbing at lines 50-60 also references `_qt_release_bin_dir` / `_qt_debug_bin_dir`. Repoint those at `_qt_bin_dir` in the same edit, or `ui-tests` loses its runtime PATH.

- [ ] **Step 6: Run the check to verify it passes, in both configurations**

```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
INSTALL_LAYOUT=flat ./build.sh
rm -rf build/check-flat && cmake --install build/build --config Release --prefix build/check-flat
powershell -File scripts/check-windows-qt-deploy.ps1 -Prefix build/check-flat -Config Release
```

Expected: `OK: Qt runtime present`. Then confirm the test suite still resolves its libraries:

```bash
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: 3/3 pass.

- [ ] **Step 7: Commit**

```bash
git add cmake/DeployQt.cmake cmake/DeployWindowsQt.cmake CMakeLists.txt tests/CMakeLists.txt scripts/check-windows-qt-deploy.ps1
git commit -m "Deploy Qt with windeployqt instead of a hardcoded cache path"
```

---

### Task 3: The packaging script

**Files:**
- Create: `package.sh`, `package-debug.sh`, `package-template.sh`
- Modify: `CMakeLists.txt:286-300` (per-platform `CPACK_PACKAGE_FILE_NAME`)

**Interfaces:**
- Consumes: `INSTALL_LAYOUT` from Task 1; the deployment from Task 2.
- Produces: `build/packages/` containing only this run's artifacts. Tasks 4, 5, 6 and 8 consume it.

- [ ] **Step 1: Write the failing test for the stale-artifact case**

Create `scripts/check-packages-dir.sh`:

```bash
#!/usr/bin/env bash
# Asserts that build/packages holds only artifacts, and nothing from a previous
# run. cpack -B leaves a _CPack_Packages staging tree -- a second full copy of
# the install tree -- and a stale artifact from an earlier run is indistinguishable
# from a fresh one once it reaches the release page.
set -euo pipefail

dir="${1:-build/packages}"

if [ ! -d "$dir" ]; then
    echo "FAIL: $dir does not exist" >&2
    exit 1
fi

if [ -d "$dir/_CPack_Packages" ]; then
    echo "FAIL: $dir/_CPack_Packages was left behind" >&2
    exit 1
fi

if [ -e "$dir/STALE-MARKER" ]; then
    echo "FAIL: $dir still contains STALE-MARKER from a previous run" >&2
    exit 1
fi

count=$(find "$dir" -maxdepth 1 -type f \
    \( -name '*.exe' -o -name '*.zip' -o -name '*.dmg' -o -name '*.deb' -o -name '*.tar.gz' \) | wc -l)
if [ "$count" -eq 0 ]; then
    echo "FAIL: no packages in $dir" >&2
    ls -la "$dir" >&2
    exit 1
fi
echo "OK: $dir holds $count package(s) and nothing stale"
```

- [ ] **Step 2: Run it to verify it fails**

```bash
chmod +x scripts/check-packages-dir.sh
mkdir -p build/packages && touch build/packages/STALE-MARKER
./scripts/check-packages-dir.sh
```

Expected: `FAIL: build/packages still contains STALE-MARKER` (and `package.sh` does not exist yet).

- [ ] **Step 3: Write `package-template.sh`**

```bash
#!/usr/bin/env bash
set -e

# Builds the distributable packages for this platform. CI calls exactly this, so
# a release can be reproduced on a developer machine -- which is the property
# that keeps the packaging rules reviewable rather than discovered on a tag.

BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) BUILD_DIR="build/build" ;;
    *)                    BUILD_DIR="build/build/$BUILD_TYPE" ;;
esac

# The install tree has to be flat, or every package carries a release/ directory
# the user has to descend through -- and the .deb would place its binary at
# /opt/slideio-viewer/release/bin/slideio-viewer.
INSTALL_LAYOUT=flat "$REPO_ROOT/build-template.sh"

# Start from nothing. cpack would happily leave the previous run's artifacts in
# place beside this one's, and the upload step cannot tell them apart.
rm -rf "$REPO_ROOT/build/packages"
mkdir -p "$REPO_ROOT/build/packages"

cpack --config "$BUILD_DIR/CPackConfig.cmake" \
      -B "$REPO_ROOT/build/packages" \
      -C "$BUILD_TYPE"

# cpack -B leaves its staging directory here: a full second copy of the installed
# tree. Left in place it triples every artifact upload and collides across
# platforms once they are merged into one directory.
rm -rf "$REPO_ROOT/build/packages/_CPack_Packages"

ls -la "$REPO_ROOT/build/packages"
```

Then `package.sh`:

```bash
#!/usr/bin/env bash
export BUILD_TYPE=Release
./package-template.sh
```

and `package-debug.sh`:

```bash
#!/usr/bin/env bash
export BUILD_TYPE=Debug
./package-template.sh
```

- [ ] **Step 4: Give each platform a self-describing package name**

In `CMakeLists.txt`, immediately before the `if(WIN32)` at line 286 (the CPack generator branch), add:

```cmake
# Self-describing artifact names, so three platforms' packages can share one
# release page without a reader having to open them to tell them apart. The .deb
# is excluded: CPACK_DEBIAN_FILE_NAME DEB-DEFAULT gives it the Debian-native
# name, which tooling depends on.
if(WIN32)
    set(_package_platform "windows-x86_64")
elseif(APPLE)
    set(_package_platform "macos-arm64")
else()
    set(_package_platform "linux-x86_64")
endif()
set(CPACK_PACKAGE_FILE_NAME "slideio-viewer-${PROJECT_VERSION}-${_package_platform}")
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
chmod +x package.sh package-debug.sh package-template.sh
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
./package.sh
./scripts/check-packages-dir.sh
```

Expected: `OK: build/packages holds 2 package(s) and nothing stale` — the NSIS `.exe` and the `.zip`, both named `slideio-viewer-0.1.0-windows-x86_64.*`, and the planted `STALE-MARKER` gone.

- [ ] **Step 6: Commit**

```bash
git add package.sh package-debug.sh package-template.sh CMakeLists.txt scripts/check-packages-dir.sh
git commit -m "Add package.sh beside build.sh"
```

---

### Task 4: Smoke-test scripts

**Files:**
- Create: `scripts/smoke-package.ps1` (Windows)
- Create: `scripts/smoke-package.sh` (Linux and macOS)

Spec §7 describes these as workflow steps; implementing them as scripts follows the spec's own Architecture section and lets a developer run the same assertions. The assertions are unchanged.

**Interfaces:**
- Consumes: `build/packages/` from Task 3.
- Produces: `scripts/smoke-package.ps1 -PackageDir <dir>` and `scripts/smoke-package.sh <package-dir>`, each exiting 0 on success. Task 8 calls them.

- [ ] **Step 1: Write the Windows smoke test**

Create `scripts/smoke-package.ps1`:

```powershell
# Unpacks the Windows archive and checks it the way a stranger would meet it:
# no build tree, no conan, no toolchain file.
param([string]$PackageDir = "build/packages")

$ErrorActionPreference = 'Stop'

$zip = Get-ChildItem $PackageDir -Filter 'slideio-viewer-*-windows-x86_64.zip' | Select-Object -First 1
if (-not $zip) { throw "no windows zip in $PackageDir" }

$unpacked = Join-Path $PackageDir 'unpacked'
if (Test-Path $unpacked) { Remove-Item -Recurse -Force $unpacked }
Expand-Archive -Path $zip.FullName -DestinationPath $unpacked -Force

# CPack nests the payload one directory deep under the package name.
$root = Get-ChildItem $unpacked -Directory | Select-Object -First 1
if (-not $root) { $root = Get-Item $unpacked }
Write-Host "unpacked to $($root.FullName)"

$required = @(
    'bin\slideio-viewer.exe', 'bin\Qt6Core.dll', 'bin\Qt6Widgets.dll',
    'bin\Qt6OpenGLWidgets.dll', 'bin\slideio.dll', 'plugins\platforms\qwindows.dll'
)
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $root.FullName $rel))) { throw "missing from package: $rel" }
}
Write-Host "OK: all required files present"

# A GUI process that stays up has resolved every library it needs to reach the
# event loop. One that exits immediately has not -- which is the failure a
# missing DLL or platform plugin actually produces.
$exe = Join-Path $root.FullName 'bin\slideio-viewer.exe'
$p = Start-Process -FilePath $exe -ArgumentList '-platform','offscreen' -PassThru
Start-Sleep -Seconds 10
if ($p.HasExited) { throw "slideio-viewer exited immediately with code $($p.ExitCode)" }
Stop-Process -Id $p.Id -Force
Write-Host "OK: slideio-viewer stayed up for 10s under -platform offscreen"
```

- [ ] **Step 2: Run it to verify it fails**

```bash
powershell -File scripts/smoke-package.ps1 -PackageDir build/packages
```

Expected: PASS if Task 2 is correct. If it throws `missing from package: plugins\platforms\qwindows.dll`, Task 2's `--plugindir` is wrong — fix that before continuing; this is the check earning its keep.

- [ ] **Step 3: Write the Unix smoke test**

Create `scripts/smoke-package.sh`:

```bash
#!/usr/bin/env bash
# Checks a built package the way a stranger would meet it: installed or unpacked,
# with no build tree, no conan and no toolchain file on the path.
set -euo pipefail

pkg_dir="${1:-build/packages}"

fail() { echo "FAIL: $*" >&2; exit 1; }

# A GUI process that stays up has resolved every library it needs to reach the
# event loop. timeout reports 124 when it has to kill the process, which is the
# success case here -- so never assert on a zero exit.
stays_up() {
    local exe="$1"; shift
    set +e
    timeout 15 "$exe" -platform offscreen "$@" >/tmp/smoke-run.log 2>&1
    local rc=$?
    set -e
    if [ "$rc" -ne 124 ]; then
        echo "--- output ---" >&2; cat /tmp/smoke-run.log >&2
        fail "$exe exited with $rc instead of staying up"
    fi
    echo "OK: $(basename "$exe") stayed up for 15s"
}

case "$(uname -s)" in
Linux)
    deb=$(find "$pkg_dir" -maxdepth 1 -name '*.deb' | head -1)
    [ -n "$deb" ] || fail "no .deb in $pkg_dir"
    # Runs as root inside the clean container Task 8 uses, and under sudo on a
    # developer machine.
    SUDO=""
    [ "$(id -u)" -eq 0 ] || SUDO="sudo"
    $SUDO apt-get update
    $SUDO apt-get install -y "$deb"

    prefix=/opt/slideio-viewer
    [ -x "$prefix/bin/slideio-viewer" ] || fail "$prefix/bin/slideio-viewer not installed"
    [ -f "$prefix/plugins/platforms/libqxcb.so" ] || fail "xcb platform plugin not packaged"

    # An unresolved library here is what the hand-written dependency list gets
    # wrong, and it is invisible until someone installs the package.
    for lib in "$prefix/bin/slideio-viewer" "$prefix/plugins/platforms/libqxcb.so"; do
        if ldd "$lib" | grep -q 'not found'; then
            ldd "$lib" | grep 'not found' >&2
            fail "unresolved libraries in $lib"
        fi
    done
    echo "OK: no unresolved libraries"

    stays_up /usr/bin/slideio-viewer
    ;;
Darwin)
    dmg=$(find "$pkg_dir" -maxdepth 1 -name '*.dmg' | head -1)
    [ -n "$dmg" ] || fail "no .dmg in $pkg_dir"
    mount_point=$(mktemp -d)
    hdiutil attach "$dmg" -mountpoint "$mount_point" -nobrowse -quiet
    rm -rf /tmp/smoke-app && mkdir -p /tmp/smoke-app
    cp -R "$mount_point"/*.app /tmp/smoke-app/
    hdiutil detach "$mount_point" -quiet

    app=$(find /tmp/smoke-app -maxdepth 1 -name '*.app' | head -1)
    [ -n "$app" ] || fail "no .app in the disk image"
    [ -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib" ] || fail "cocoa platform plugin not bundled"

    # An absolute build-machine path here means the bundle works for whoever
    # built it and nobody else -- exactly the breakage the old Windows glob was.
    bad=$(find "$app" -type f \( -name '*.dylib' -o -perm +111 \) -exec otool -L {} + 2>/dev/null \
          | grep -E '/Users/|\.conan2|/conan2/' || true)
    if [ -n "$bad" ]; then
        echo "$bad" >&2
        fail "bundle references build-machine paths"
    fi
    echo "OK: no build-machine paths in the bundle"

    stays_up "$app/Contents/MacOS/slideio-viewer"
    ;;
*)
    fail "unsupported platform $(uname -s)"
    ;;
esac

echo "OK: package smoke test passed"
```

- [ ] **Step 4: Verify the script parses**

```bash
chmod +x scripts/smoke-package.sh
bash -n scripts/smoke-package.sh && echo "syntax OK"
```

Expected: `syntax OK`. The Linux and macOS branches run for the first time in Task 8; there is no way to exercise them from the Windows development machine, and pretending otherwise would be worse than saying so.

- [ ] **Step 5: Commit**

```bash
git add scripts/smoke-package.ps1 scripts/smoke-package.sh
git commit -m "Smoke-test a built package the way a stranger meets it"
```

---

### Task 5: Linux packaging

**Files:**
- Create: `cmake/CPackLinuxOptions.cmake`
- Create: `resources/slideio-viewer.desktop`
- Modify: `CMakeLists.txt` — the Qt deployment block from Task 2 (Linux branch) and the CPack `else()` branch

**Interfaces:**
- Consumes: `slideio_viewer_qt_bin_dir` (Task 2), `_install_bin` / `_install_lib` / `_install_plugins` (Task 1).
- Produces: a `.deb` laid out per spec §3, verified in Task 8.

- [ ] **Step 1: Write the desktop entry**

Create `resources/slideio-viewer.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=SlideIO Viewer
GenericName=Whole-Slide Image Viewer
Comment=View and navigate digital pathology whole-slide images
Exec=/opt/slideio-viewer/bin/slideio-viewer %f
Icon=slideio-viewer
Terminal=false
Categories=Graphics;Science;Viewer;
MimeType=image/tiff;
```

- [ ] **Step 2: Add the Linux Qt deployment**

In `CMakeLists.txt`, extend the Qt deployment block from Task 2 with a Linux branch:

```cmake
if(UNIX AND NOT APPLE)
    # No deployment tool ships for Linux, so the module set is named explicitly.
    # These are the modules slideio-viewer-ui links, plus the DBus and XcbQpa
    # libraries the xcb platform plugin pulls in.
    get_filename_component(_qt_root "${_qt_bin_dir}" DIRECTORY)
    set(_qt_libs Core Gui Widgets OpenGL OpenGLWidgets Network DBus XcbQpa)
    foreach(_mod ${_qt_libs})
        file(GLOB _qt_lib_files "${_qt_root}/lib/libQt6${_mod}.so*")
        if(NOT _qt_lib_files)
            message(FATAL_ERROR "Qt module libQt6${_mod}.so not found under ${_qt_root}/lib")
        endif()
        install(FILES ${_qt_lib_files} DESTINATION ${_install_lib})
    endforeach()
    foreach(_plugin_dir platforms xcbglintegrations styles imageformats)
        if(NOT EXISTS "${_qt_root}/plugins/${_plugin_dir}")
            message(FATAL_ERROR "Qt plugin directory '${_plugin_dir}' not found under ${_qt_root}/plugins")
        endif()
        install(DIRECTORY "${_qt_root}/plugins/${_plugin_dir}" DESTINATION ${_install_plugins})
    endforeach()

    # SlideIO's own shared libraries, which have no Linux equivalent of the
    # CopySlideIODlls.cmake step the Windows build uses.
    install(DIRECTORY "${SLIDEIO_ROOT}/release/lib/"
        DESTINATION ${_install_lib}
        FILES_MATCHING PATTERN "*.so*")

    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/slideio-viewer.desktop"
        DESTINATION "share/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.png"
        DESTINATION "share/icons/hicolor/256x256/apps"
        RENAME "slideio-viewer.png")
endif()
```

Give the executable an RPATH that finds the bundled libraries, next to the existing `if(APPLE)` RPATH block at `CMakeLists.txt:83`:

```cmake
if(UNIX AND NOT APPLE)
    set_target_properties(slideio-viewer PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib")
endif()
```

- [ ] **Step 3: Write the per-generator CPack options**

Create `cmake/CPackLinuxOptions.cmake`:

```cmake
# CPACK_PACKAGING_INSTALL_PREFIX is global, but the two Linux generators need
# different prefixes: the .deb places files at absolute system paths under /opt
# and /usr, while the .tar.gz has to stay relocatable. CPack re-evaluates this
# file once per generator, which is the mechanism provided for exactly that.

if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/slideio-viewer")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_NAME "slideio-viewer")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Booritas/slideio")

    # Off deliberately. With it on, dpkg scans the bundled Qt libraries and adds
    # dependencies on the distribution's own Qt packages -- the opposite of
    # bundling, and a package that then pulls in a second, conflicting Qt.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)

    # What Qt still needs from the system even with Qt bundled: the xcb platform
    # plugin's own dependencies. Task 8 derives the authoritative list by running
    # ldd over the packaged libqxcb.so on the runner; this is the starting point,
    # not a guess to be trusted.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libgl1, libglx-mesa0, libxkbcommon0, libxkbcommon-x11-0, \
libfontconfig1, libfreetype6, libdbus-1-3, libxcb1, libxcb-cursor0, libxcb-icccm4, \
libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, \
libxcb-sync1, libxcb-xfixes0, libxcb-xinerama0, libxcb-xkb1, libx11-xcb1")

    # The /usr/bin symlink and the desktop database refresh. The binary itself
    # stays in /opt so the bundled libraries and plugins travel with it.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_LIST_DIR}/debian/postinst;${CMAKE_CURRENT_LIST_DIR}/debian/prerm")
else()
    # Relocatable: bin/, lib/ and plugins/ at the root of the tarball.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
endif()
```

Spec §3 lists the desktop entry and icon at `/usr/share/...` while the payload sits
under `/opt/slideio-viewer`. Rather than split the install into two prefixes — which
`CPACK_PACKAGING_INSTALL_PREFIX` cannot express in one generator — everything is
packaged under `/opt/slideio-viewer` and the maintainer scripts link the three paths
that have to appear in `/usr`. Same end state, and the CMake destinations stay flat.

Create `cmake/debian/postinst`:

```sh
#!/bin/sh
set -e

ln -sf /opt/slideio-viewer/bin/slideio-viewer /usr/bin/slideio-viewer

mkdir -p /usr/share/applications /usr/share/icons/hicolor/256x256/apps
ln -sf /opt/slideio-viewer/share/applications/slideio-viewer.desktop \
       /usr/share/applications/slideio-viewer.desktop
ln -sf /opt/slideio-viewer/share/icons/hicolor/256x256/apps/slideio-viewer.png \
       /usr/share/icons/hicolor/256x256/apps/slideio-viewer.png

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
exit 0
```

Create `cmake/debian/prerm`:

```sh
#!/bin/sh
set -e
for link in /usr/bin/slideio-viewer \
            /usr/share/applications/slideio-viewer.desktop \
            /usr/share/icons/hicolor/256x256/apps/slideio-viewer.png; do
    if [ -L "$link" ]; then
        rm -f "$link"
    fi
done
exit 0
```

Both must be executable: `chmod 755 cmake/debian/postinst cmake/debian/prerm`.

- [ ] **Step 4: Point the CPack `else()` branch at the config file**

Replace the `else()` branch at `CMakeLists.txt:299`:

```cmake
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackLinuxOptions.cmake")
endif()
```

- [ ] **Step 5: Verify what can be verified from Windows**

```bash
bash -n cmake/debian/postinst && bash -n cmake/debian/prerm && echo "maintainer scripts OK"
grep -q '^Exec=/opt/slideio-viewer/bin/slideio-viewer' resources/slideio-viewer.desktop && echo "desktop entry OK"
```

Expected: both print OK. The `.deb` itself is built and smoke-tested for the first time in Task 8 — there is no Linux toolchain on this machine, and a local `cmake` configure would fail at `find_package(SlideIO)` before reaching any of this.

- [ ] **Step 6: Commit**

```bash
git add cmake/CPackLinuxOptions.cmake cmake/debian resources/slideio-viewer.desktop CMakeLists.txt
git commit -m "Package the viewer and its Qt for Debian"
```

---

### Task 6: macOS disk image under the flat layout

**Files:**
- Modify: `cmake/DeployMacOSBundle.cmake`
- Modify: `CMakeLists.txt` — macOS CPack branch

**Interfaces:**
- Consumes: `_install_app` (Task 1), `SLIDEIO_VIEWER_APP_DIR` (Task 1).
- Produces: `slideio-viewer-<version>-macos-arm64.dmg` with the `.app` at its root.

- [ ] **Step 1: Make the SlideIO dylib source layout-independent**

`cmake/DeployMacOSBundle.cmake:36` reads `${SLIDEIO_ROOT}/${_cfg}/bin`, where `_cfg` comes from `CMAKE_INSTALL_CONFIG_NAME`. That indexes SlideIO's install tree, not ours, so it stays — but the `_cfg` variable is now only used there. Confirm the file reads:

```cmake
set(_bundle "${SLIDEIO_VIEWER_APP_DIR}/slideio-viewer.app")

if(NOT EXISTS "${_bundle}")
    message(FATAL_ERROR "DeployMacOSBundle: bundle not found at '${_bundle}'")
endif()
```

and that `string(TOLOWER "${CMAKE_INSTALL_CONFIG_NAME}" _cfg)` remains above the `_slideio_src_dir` line.

- [ ] **Step 2: Set the disk image volume name**

In the `elseif(APPLE)` CPack branch at `CMakeLists.txt:297`:

```cmake
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "SlideIO Viewer ${PROJECT_VERSION}")
```

- [ ] **Step 3: Verify the file parses**

```bash
cmake -P cmake/DeployMacOSBundle.cmake 2>&1 | head -3
```

Expected: a `FATAL_ERROR` about the bundle not being found — which proves the file parses and that `SLIDEIO_VIEWER_APP_DIR` is the variable it reads. A syntax error would report a different message. The `.dmg` is built for the first time in Task 8.

- [ ] **Step 4: Commit**

```bash
git add cmake/DeployMacOSBundle.cmake CMakeLists.txt
git commit -m "Put the app at the root of the disk image"
```

---

### Task 7: `build-validation.yml`

**Files:**
- Create: `.github/workflows/build-validation.yml`
- Create: `scripts/verify-slideio-install.sh`

**Interfaces:**
- Consumes: `build.sh`.
- Produces: the reusable setup step sequence Task 8 repeats.

- [ ] **Step 1: Write the failing test for the partial-cache case**

Create `scripts/verify-slideio-install.sh`:

```bash
#!/usr/bin/env bash
# build-template.sh skips the SlideIO submodule build when one header exists. A
# cache saved from a cancelled run can satisfy that check while carrying no
# libraries, and the viewer then links against a half-built install -- a failure
# that looks like a source problem and is not. Discard such a tree so the build
# rebuilds it.
set -euo pipefail

root="${1:-extern/slideio/build/install}"

if [ ! -d "$root/release" ]; then
    echo "no SlideIO install at $root; nothing to verify"
    exit 0
fi

ok=1
[ -f "$root/release/include/slideio/slideio/slideio.hpp" ] || ok=0
# One of the two library naming conventions must be present.
ls "$root"/release/lib/libslideio.* >/dev/null 2>&1 || \
  ls "$root"/release/lib/slideio.lib >/dev/null 2>&1 || ok=0

if [ "$ok" -eq 0 ]; then
    echo "SlideIO install at $root is incomplete; discarding it so it is rebuilt"
    rm -rf "$root"
    exit 0
fi
echo "OK: SlideIO install at $root is complete"
```

- [ ] **Step 2: Run it to verify it detects a partial tree**

```bash
chmod +x scripts/verify-slideio-install.sh
rm -rf /tmp/fake-slideio
mkdir -p /tmp/fake-slideio/release/include/slideio/slideio /tmp/fake-slideio/release/lib
touch /tmp/fake-slideio/release/include/slideio/slideio/slideio.hpp
./scripts/verify-slideio-install.sh /tmp/fake-slideio
[ -d /tmp/fake-slideio ] && echo "FAIL: partial tree survived" || echo "PASS: partial tree discarded"
```

Expected: `PASS: partial tree discarded`. Then confirm it leaves a real tree alone:

```bash
./scripts/verify-slideio-install.sh extern/slideio/build/install
```

Expected: `OK: SlideIO install at ... is complete`.

- [ ] **Step 3: Write the workflow**

Create `.github/workflows/build-validation.yml`:

```yaml
name: Build Validation

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  workflow_dispatch:

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build:
    name: ${{ matrix.name }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          # Pinned, never -latest. ubuntu-22.04 fixes the glibc floor at 2.35 so
          # the .deb installs on Ubuntu 22.04+ and Debian 12+; macos-14 is the
          # first arm64 image and naming it keeps the architecture from changing
          # under us; windows-2022 matches compiler.version=194 in the profile.
          - { name: "Windows x86_64", os: windows-2022, test_dir: "build/build",         ctest_config: "-C Release" }
          - { name: "Debian x86_64",  os: ubuntu-22.04, test_dir: "build/build/Release", ctest_config: "" }
          - { name: "macOS arm64",    os: macos-14,     test_dir: "build/build/Release", ctest_config: "" }

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - uses: actions/setup-python@v5
        with:
          python-version: '3.x'

      - name: Install Conan
        run: pip install conan

      - name: Setup MSVC
        if: runner.os == 'Windows'
        uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: amd64

      # Qt's xcb platform plugin links these, and the SlideIO build needs the
      # OpenGL headers. Without them the Qt conan package fails to build and the
      # error names a missing X header rather than the cause.
      - name: Install Linux build dependencies
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            libgl1-mesa-dev libglu1-mesa-dev libxkbcommon-dev libxkbcommon-x11-dev \
            libx11-xcb-dev libxcb-cursor-dev libxcb-icccm4-dev libxcb-image0-dev \
            libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render-util0-dev \
            libxcb-shape0-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-xinerama0-dev \
            libxcb-xkb-dev libfontconfig1-dev libfreetype6-dev libdbus-1-dev

      - name: Read the SlideIO submodule revision
        id: slideio_rev
        shell: bash
        run: echo "sha=$(git -C extern/slideio rev-parse HEAD)" >> "$GITHUB_OUTPUT"

      - name: Cache Conan packages
        uses: actions/cache@v4
        with:
          path: ~/.conan2
          key: conan-${{ matrix.os }}-${{ hashFiles('conanfile.py', 'conan/profiles/**') }}
          restore-keys: conan-${{ matrix.os }}-

      - name: Cache the SlideIO install tree
        uses: actions/cache@v4
        with:
          path: extern/slideio/build/install
          key: slideio-${{ matrix.os }}-${{ steps.slideio_rev.outputs.sha }}

      # A restored cache is trusted by build-template.sh on the strength of one
      # header. Check it carries libraries too before letting the build rely on it.
      - name: Verify the restored SlideIO install
        shell: bash
        run: ./scripts/verify-slideio-install.sh extern/slideio/build/install

      - name: Build
        shell: bash
        env:
          # Note the lowercase l: build-template.sh spells it that way.
          CONAN_PROFIlE: ${{ runner.os == 'Linux' && 'conan/profiles/Linux/release-ci' || '' }}
        run: |
          set -euo pipefail
          if [ -z "${CONAN_PROFIlE}" ]; then unset CONAN_PROFIlE; fi
          ./build.sh

      - name: Test
        shell: bash
        run: ctest --test-dir ${{ matrix.test_dir }} ${{ matrix.ctest_config }} --output-on-failure
```

- [ ] **Step 4: Add the CI Conan profile for Linux**

`conan/profiles/Linux/release` pins `compiler.version=14`, which `ubuntu-22.04` does not carry. Create `conan/profiles/Linux/release-ci` as a copy with `compiler.version` set to the gcc the runner actually provides. Determine it from the first workflow run's log (`gcc --version`), then commit the profile with that value — do not guess it here.

Until that value is known, the Linux job will fail at `conan install` with a profile error naming the compiler version, which is the expected first-run outcome.

- [ ] **Step 5: Lint and push**

```bash
actionlint .github/workflows/build-validation.yml
git add .github/workflows/build-validation.yml scripts/verify-slideio-install.sh
git commit -m "Add the per-commit build and test gate"
git push
```

Expected: `actionlint` silent; the workflow appears in the Actions tab and runs. Read the Linux job's log for the gcc version, add `conan/profiles/Linux/release-ci`, and commit it.

- [ ] **Step 6: Commit the profile**

```bash
git add conan/profiles/Linux/release-ci
git commit -m "Pin a Linux CI conan profile to the runner's compiler"
git push
```

Expected: all three jobs green with `ctest` reporting 3/3.

---

### Task 8: `release.yml`

**Files:**
- Create: `.github/workflows/release.yml`
- Create: `scripts/check-version.sh`

**Interfaces:**
- Consumes: `package.sh` (Task 3), `scripts/smoke-package.*` (Task 4), the setup sequence from Task 7.
- Produces: a draft GitHub Release carrying all three platforms' artifacts.

- [ ] **Step 1: Write the failing test for the version check**

Create `scripts/check-version.sh`:

```bash
#!/usr/bin/env bash
# Reads the project version and, when given a tag, requires it to agree.
# Usage: check-version.sh [<tag>]
set -euo pipefail

version=$(sed -n 's/^project(slideio-viewer VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
if [ -z "$version" ]; then
    echo "Could not read the version from CMakeLists.txt" >&2
    exit 1
fi

tag="${1:-}"
if [ -n "$tag" ]; then
    # A prerelease tag is legitimate: v0.2.0-rc1 releases the 0.2.0 tree. Compare
    # the version core and let a suffix through, rather than rejecting the
    # release candidate or -- worse -- accepting v0.2.0-rc1 for a 0.3.0 tree.
    tag_version="${tag#v}"
    tag_core="${tag_version%%-*}"
    if [ "$tag_core" != "$version" ]; then
        echo "Tag ${tag} does not match project version ${version} in CMakeLists.txt." >&2
        echo "Update the project() version or retag." >&2
        exit 1
    fi
fi

echo "$version"
```

Create `tests/scripts/check-version-test.sh`:

```bash
#!/usr/bin/env bash
# The version check is the thing standing between a mistyped tag and three
# platforms spending an hour producing packages whose names are wrong.
set -uo pipefail
cd "$(dirname "$0")/../.."

pass=0; fail=0
expect_ok()   { if ./scripts/check-version.sh "$1" >/dev/null 2>&1; then pass=$((pass+1)); else echo "FAIL: expected '$1' accepted"; fail=$((fail+1)); fi; }
expect_fail() { if ./scripts/check-version.sh "$1" >/dev/null 2>&1; then echo "FAIL: expected '$1' rejected"; fail=$((fail+1)); else pass=$((pass+1)); fi; }

version=$(./scripts/check-version.sh)
[ -n "$version" ] && pass=$((pass+1)) || { echo "FAIL: no version read"; fail=$((fail+1)); }

expect_ok   "v${version}"
expect_ok   "v${version}-rc1"          # a release candidate for this tree
expect_fail "v9.9.9"                   # a tag for a different version
expect_fail "v9.9.9-rc1"               # a prerelease of a different version
expect_fail "not-a-tag"

echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
chmod +x scripts/check-version.sh tests/scripts/check-version-test.sh
./tests/scripts/check-version-test.sh
```

Expected: FAIL — `scripts/check-version.sh` has just been written, so run it and confirm every case passes; if `v9.9.9-rc1` is accepted, the `%%-*` core comparison is wrong and must be fixed before continuing.

- [ ] **Step 3: Confirm it passes**

```bash
./tests/scripts/check-version-test.sh
```

Expected: `passed 6, failed 0`.

- [ ] **Step 4: Write the release workflow**

Create `.github/workflows/release.yml`:

```yaml
name: Release

# Builds the distributable packages for a version tag and attaches them to a
# draft GitHub Release:
#
#   slideio-viewer-<version>-windows-x86_64.exe   NSIS installer
#   slideio-viewer-<version>-windows-x86_64.zip   the same tree, unpacked
#   slideio-viewer-<version>-macos-arm64.dmg      Apple Silicon, unsigned
#   slideio-viewer_<version>_amd64.deb            Ubuntu 22.04+ / Debian 12+
#
# Everything is produced by ./package.sh, so the same artifacts can be built on a
# developer machine -- see docs/superpowers/specs/2026-10-03-ci-distribution-design.md.
#
# build-validation.yml remains the per-commit gate and is untouched. A manual run
# builds and smoke-tests every artifact without publishing anything.

on:
  push:
    tags: ['v*']
  workflow_dispatch:
    inputs:
      platforms:
        description: "Which platforms to build"
        required: true
        default: "all"
        type: choice
        options: [all, windows, debian, macos]
      skip_tests:
        description: "Skip ctest (packaging changes only)"
        required: false
        default: false
        type: boolean

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: false

jobs:
  check-version:
    name: Check version
    runs-on: ubuntu-latest
    outputs:
      version: ${{ steps.version.outputs.version }}
      matrix: ${{ steps.matrix.outputs.matrix }}
    steps:
      - uses: actions/checkout@v4

      - name: Compare the tag with the project version
        id: version
        shell: bash
        run: |
          set -euo pipefail
          if [ "${GITHUB_REF_TYPE:-}" = "tag" ]; then
            version=$(./scripts/check-version.sh "${GITHUB_REF_NAME}")
          else
            version=$(./scripts/check-version.sh)
          fi
          echo "version: ${version}"
          echo "version=${version}" >> "$GITHUB_OUTPUT"

      # Assembled here rather than inline below so a manual run can ask for one
      # platform. A tag push carries no inputs, so a release always builds three.
      - name: Select the platforms to build
        id: matrix
        shell: bash
        env:
          PLATFORMS: ${{ inputs.platforms || 'all' }}
        run: |
          set -euo pipefail
          windows='{"name":"Windows x86_64","os":"windows-2022","artifact":"windows-x86_64","test_dir":"build/build","ctest_config":"-C Release"}'
          debian='{"name":"Debian x86_64","os":"ubuntu-22.04","artifact":"debian-x86_64","test_dir":"build/build/Release","ctest_config":""}'
          macos='{"name":"macOS arm64","os":"macos-14","artifact":"macos-arm64","test_dir":"build/build/Release","ctest_config":""}'
          case "$PLATFORMS" in
            all)     include="${windows},${debian},${macos}" ;;
            windows) include="${windows}" ;;
            debian)  include="${debian}" ;;
            macos)   include="${macos}" ;;
            *) echo "Unknown platform selection: ${PLATFORMS}" >&2; exit 1 ;;
          esac
          echo "matrix={\"include\":[${include}]}" >> "$GITHUB_OUTPUT"

  build:
    name: ${{ matrix.name }}
    needs: check-version
    runs-on: ${{ matrix.os }}
    strategy:
      # One platform failing should not hide whether the other two package
      # cleanly; a release short one artifact is worth knowing about in full.
      fail-fast: false
      matrix: ${{ fromJson(needs.check-version.outputs.matrix) }}

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - uses: actions/setup-python@v5
        with:
          python-version: '3.x'

      - name: Install Conan
        run: pip install conan

      - name: Setup MSVC
        if: runner.os == 'Windows'
        uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: amd64

      - name: Install Linux build dependencies
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            libgl1-mesa-dev libglu1-mesa-dev libxkbcommon-dev libxkbcommon-x11-dev \
            libx11-xcb-dev libxcb-cursor-dev libxcb-icccm4-dev libxcb-image0-dev \
            libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render-util0-dev \
            libxcb-shape0-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-xinerama0-dev \
            libxcb-xkb-dev libfontconfig1-dev libfreetype6-dev libdbus-1-dev

      - name: Read the SlideIO submodule revision
        id: slideio_rev
        shell: bash
        run: echo "sha=$(git -C extern/slideio rev-parse HEAD)" >> "$GITHUB_OUTPUT"

      - name: Cache Conan packages
        uses: actions/cache@v4
        with:
          path: ~/.conan2
          key: conan-${{ matrix.os }}-${{ hashFiles('conanfile.py', 'conan/profiles/**') }}
          restore-keys: conan-${{ matrix.os }}-

      - name: Cache the SlideIO install tree
        uses: actions/cache@v4
        with:
          path: extern/slideio/build/install
          key: slideio-${{ matrix.os }}-${{ steps.slideio_rev.outputs.sha }}

      - name: Verify the restored SlideIO install
        shell: bash
        run: ./scripts/verify-slideio-install.sh extern/slideio/build/install

      - name: Build the packages
        shell: bash
        env:
          CONAN_PROFIlE: ${{ runner.os == 'Linux' && 'conan/profiles/Linux/release-ci' || '' }}
        run: |
          set -euo pipefail
          if [ -z "${CONAN_PROFIlE}" ]; then unset CONAN_PROFIlE; fi
          ./package.sh

      - name: Test
        # Skippable only from a manual run, which cannot publish -- see the
        # publish job's condition. A tag push carries no inputs.
        if: ${{ !inputs.skip_tests }}
        shell: bash
        run: ctest --test-dir ${{ matrix.test_dir }} ${{ matrix.ctest_config }} --output-on-failure

      # In a clean container, not on the runner. The build steps above installed
      # libxcb-*-dev, libgl1-mesa-dev and the rest, so a smoke test run here would
      # find every dependency satisfied no matter what the package declares --
      # which is the one thing this test exists to catch. A system Qt goes in
      # first, so the bundled plugins have to win rather than merely be the only
      # ones present.
      - name: Smoke test the Debian package in a clean container
        if: runner.os == 'Linux'
        run: |
          docker run --rm \
            -v "${{ github.workspace }}:/work" -w /work \
            ubuntu:22.04 \
            bash -c '
              set -euo pipefail
              export DEBIAN_FRONTEND=noninteractive
              apt-get update
              apt-get install -y sudo file binutils qt6-base-dev
              ./scripts/smoke-package.sh build/packages
              # What the packaged plugin actually needs. The dependency list in
              # cmake/CPackLinuxOptions.cmake is corrected from this rather than
              # carried forward on faith.
              echo "--- libqxcb.so dependencies ---"
              ldd /opt/slideio-viewer/plugins/platforms/libqxcb.so || true
            '

      - name: Smoke test the package
        if: runner.os == 'macOS'
        shell: bash
        run: ./scripts/smoke-package.sh build/packages

      - name: Smoke test the package
        if: runner.os == 'Windows'
        shell: pwsh
        run: ./scripts/smoke-package.ps1 -PackageDir build/packages

      - name: Upload the packages
        uses: actions/upload-artifact@v4
        with:
          name: slideio-viewer-${{ matrix.artifact }}
          # Named extensions, not build/packages/* -- cpack's staging directory
          # is a full second copy of the install tree and would be swept up.
          path: |
            build/packages/*.exe
            build/packages/*.zip
            build/packages/*.dmg
            build/packages/*.deb
            build/packages/*.tar.gz
          if-no-files-found: error
          retention-days: 30

  publish:
    name: Publish release
    needs: [check-version, build]
    # Only a tag push publishes. Both halves are load-bearing: the dispatch API
    # takes a tag as readily as a branch, so ref_type alone would let a manual
    # run that built one platform with the tests skipped publish a release.
    if: github.event_name == 'push' && github.ref_type == 'tag'
    runs-on: ubuntu-latest
    permissions:
      contents: write

    steps:
      - name: Download every platform's packages
        uses: actions/download-artifact@v4
        with:
          path: dist
          merge-multiple: true

      - name: List what will be published
        run: ls -la dist

      - name: Create the GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: dist/*
          name: SlideIO Viewer ${{ needs.check-version.outputs.version }}
          draft: true
          generate_release_notes: true
          fail_on_unmatched_files: true
          body: |
            Binary distributions of SlideIO Viewer ${{ needs.check-version.outputs.version }}.

            | Platform | File | Notes |
            |---|---|---|
            | Windows x86_64 | `slideio-viewer-${{ needs.check-version.outputs.version }}-windows-x86_64.exe` | Installer. Unsigned: SmartScreen will warn on first run. |
            | Windows x86_64 | `slideio-viewer-${{ needs.check-version.outputs.version }}-windows-x86_64.zip` | The same tree, to unpack anywhere. |
            | macOS arm64 | `slideio-viewer-${{ needs.check-version.outputs.version }}-macos-arm64.dmg` | Apple Silicon. Unsigned — see below. |
            | Debian/Ubuntu x86_64 | `slideio-viewer_${{ needs.check-version.outputs.version }}_amd64.deb` | Ubuntu 22.04+ / Debian 12+. Qt is bundled. |

            The macOS disk image is not signed or notarised, so Gatekeeper refuses
            it on first open. Right-click the application and choose **Open**, or
            run:

            ```
            xattr -dr com.apple.quarantine "/Applications/slideio-viewer.app"
            ```
```

- [ ] **Step 5: Lint, push, and rehearse each platform**

```bash
actionlint .github/workflows/release.yml
git add .github/workflows/release.yml scripts/check-version.sh tests/scripts/check-version-test.sh
git commit -m "Add the tag-driven release pipeline"
git push
```

Then, from the Actions tab, dispatch `Release` three times — once with `platforms: windows`, once `debian`, once `macos`. GitHub offers the Run workflow button only for workflow files on the default branch, so this requires the commit above to be on `main`.

Expected per run: the package is produced, the smoke test passes, the artifact appears, and no release is created. Fix what the Linux and macOS runs find — this is where Tasks 5 and 6 are verified for the first time, and where the Debian dependency list gets corrected from the `ldd` output the run prints.

- [ ] **Step 6: Correct the Debian dependency list from evidence**

Read the `--- libqxcb.so dependencies ---` output from the container step. Any `.so` it names that is not under `/opt/slideio-viewer/lib` must map to a package in `CPACK_DEBIAN_PACKAGE_DEPENDS`; `dpkg -S <path>` inside the same container names the package for a given library. Update `cmake/CPackLinuxOptions.cmake`, commit, and re-dispatch the Debian run.

```bash
git add cmake/CPackLinuxOptions.cmake
git commit -m "Depend on the libraries the packaged plugin actually needs"
git push
```

Expected: the Debian dispatch run is green including the smoke test, with system Qt installed alongside.

- [ ] **Step 7: Rehearse a full run and a real tag**

```bash
# All three platforms, still publishing nothing.
# (dispatch Release with platforms: all)

# Then the real thing:
git tag v0.1.0
git push origin v0.1.0
```

Expected: `check-version` passes, three `build` jobs succeed, `publish` creates a **draft** release with four files attached. Review the draft before publishing it.

---

## Notes for the implementer

- The Windows development machine can verify Tasks 1-4 and the script-level tests in Tasks 7 and 8. Tasks 5 and 6 are verified by the dispatch runs in Task 8 Step 5. This is stated rather than papered over: there is no Linux or macOS toolchain here.
- Conan must be on PATH: `export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"` before any `build.sh` or `package.sh`.
- The largest schedule risk is the first CI run on each platform compiling Qt from source, because Conan Center may not publish a binary for `qt/6.7.3` with `shared=True, qtshadertools=True, opengl=desktop`. If a job approaches the six-hour limit, the mitigations in preference order are in spec §8: relax the option set to one Conan Center builds for, or add a manually dispatched warm-up job whose only purpose is to populate `~/.conan2`.
