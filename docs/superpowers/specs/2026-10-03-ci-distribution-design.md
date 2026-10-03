# CI and binary distribution — design

**Date:** 2026-10-03
**Status:** Draft (awaiting review)

## Goal

Give the viewer the same two-workflow CI the SlideIO library has, and have it
produce an installable artifact for each supported platform:

| Platform | Runner | Artifact |
|---|---|---|
| Windows x86_64 | `windows-2022` | `slideio-viewer-<version>-windows-x86_64.exe` (NSIS) and `.zip` |
| macOS arm64 | `macos-14` | `slideio-viewer-<version>-macos-arm64.dmg` |
| Debian/Ubuntu x86_64 | `ubuntu-22.04` | `slideio-viewer_<version>_amd64.deb` and a `.tar.gz` |

`build-validation.yml` is the per-commit gate: configure, build and run `ctest`
on all three platforms. `release.yml` runs on a `v*` tag, builds the packages,
smoke-tests each one against the *installed* artifact, and attaches them to a
draft GitHub Release.

## Non-goals

- **No code signing or notarisation.** The `.dmg` and the `.exe` ship unsigned,
  as SlideIO's own artifacts do. The release notes carry the Gatekeeper
  workaround. Wiring `notarytool` is a later change, gated on an Apple Developer
  account existing.
- **No Linux distributions other than Debian/Ubuntu.** No AppImage, no RPM, no
  Flatpak. The `.tar.gz` is a by-product of the same install tree, not a
  supported portable build.
- **No 32-bit, no x86_64 macOS, no arm64 Linux or Windows.**
- **No debug distribution.** Release configuration only, matching SlideIO.
- **No publishing to any package index** (winget, Homebrew, an apt repository).
- **No GUI automation in CI.** The smoke tests check that a packaged binary
  resolves its libraries and survives a headless start; they do not drive the
  interface.

## Decisions taken before this spec

1. **SlideIO comes from the submodule**, built by `build-template.sh` exactly as
   a developer builds it, with `extern/slideio/build/install` restored from a
   cache keyed on the submodule revision. Consuming SlideIO's published release
   archives was rejected: they are flat (`bin/`, `lib/`, `include/` at the root)
   while `cmake/FindSlideIO.cmake` requires a `release/` + `debug/` layout, so it
   would need a repacking shim, and it would not work for an unreleased submodule
   revision.
2. **Qt is bundled into the Debian package** under `/opt/slideio-viewer`, so the
   `.deb` carries the same Qt 6.7.3 the application is tested against rather than
   whatever the distribution ships.
3. **Both workflows**, mirroring SlideIO's split.
4. **Unsigned macOS artifact.**

## Architecture

The guiding constraint is SlideIO's, stated in its own `release.yml`:
*"Everything is produced by `install.py -a package-only`, so the same artifacts
can be built on a developer machine."* Every platform-specific rule therefore
lives in CMake or in a script a developer can run; the workflow files only
sequence those steps, cache, and upload.

```
                 build.sh ──────────────► existing: conan, SlideIO submodule,
                    │                     cmake configure / build / install
                    │
   new ───►  package.sh ──► build.sh (layout=flat) ──► cpack ──► build/packages/
                    │
                    ▼
   .github/workflows/release.yml           calls package.sh, smoke-tests, uploads
   .github/workflows/build-validation.yml  calls build.sh, runs ctest
```

This split is also why the Windows Qt deployment rotted unnoticed: it is
reachable only through `cmake --install`, which no one runs on a clean machine.
Moving it behind a script that CI exercises on every tag is what keeps it honest.

## 1. Qt discovery and deployment — `cmake/DeployQt.cmake`

`CMakeLists.txt:177` currently finds the Qt runtime with
`file(GLOB_RECURSE … "d:/conan2/p/b/qt*/p/bin/${_dll_name}")`, a path that
exists only on one developer machine, and installs the results with `OPTIONAL`.
On any other machine the glob misses, the fallback over `CMAKE_PREFIX_PATH` is
not guaranteed to hit, and `OPTIONAL` turns the miss into an installer that
builds cleanly and contains no Qt at all.

Replace it with the mechanism `CMakeLists.txt:91` already uses for macOS:

```cmake
get_target_property(_qmake_loc Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_loc}" DIRECTORY)
```

`Qt6::qmake` is an imported target Conan's `CMakeDeps` generates, so this
resolves wherever the Conan cache lives. From `_qt_bin_dir`:

- **Windows** — run `windeployqt.exe` against the installed executable at
  install time (`install(CODE …)`), the mirror of what `DeployMacOSBundle.cmake`
  does with `macdeployqt`. It resolves the module set, the platform plugin and
  the style plugins from the binary itself, which is more durable than the
  current hand-maintained six-module list.
- **Linux** — no Qt deployment exists today. Copy the needed `libQt6*.so.6` and
  the `platforms`, `xcbglintegrations`, `styles` and `imageformats` plugin
  directories into the staged tree (§3).
- **macOS** — unchanged; `DeployMacOSBundle.cmake` already works.

A missing deployment tool becomes a `FATAL_ERROR` rather than a warning, so the
failure lands in the build that caused it. Where an explicit module list is still
installed by file — the Linux branch, and the Windows fallback below — those
`install()` calls lose `OPTIONAL` for the same reason.

**To verify during implementation:** that the Conan `qt/6.7.3` package ships
`windeployqt` in `bin/`. If it does not, fall back to installing an explicit
module list resolved from `_qt_bin_dir` — the current list, minus the hardcoded
path and minus `OPTIONAL`.

## 2. Install layout — `SLIDEIO_VIEWER_INSTALL_LAYOUT`

`CMakeLists.txt:148` sets install destinations to `$<LOWER_CASE:$<CONFIG>>/bin`,
which is right for the developer workflow (`build/install/release/bin` and
`build/install/debug/bin` side by side) and wrong for every package: a `.deb`
built from it would contain `/opt/slideio-viewer/release/bin/slideio-viewer`.

Add a cache option:

- `SLIDEIO_VIEWER_INSTALL_LAYOUT=per-config` (default) — today's behaviour.
- `SLIDEIO_VIEWER_INSTALL_LAYOUT=flat` — `bin/`, `lib/`, `plugins/` directly
  under the prefix. `package.sh` sets this.

Three places derive the per-config path and must follow the option rather than
recomputing it: the `qt.conf` writer at `CMakeLists.txt:241`,
`cmake/DeployMacOSBundle.cmake` (`_bundle` and `_slideio_src_dir`), and the
`install(TARGETS)` destinations.

On macOS the `DragNDrop` generator wants `slideio-viewer.app` at the root of the
disk image, so under `flat` the bundle installs to `.` rather than `bin/`.

## 3. Linux packaging

Target layout, self-contained under one prefix:

```
/opt/slideio-viewer/bin/slideio-viewer      RPATH $ORIGIN/../lib
/opt/slideio-viewer/bin/qt.conf             [Paths] Plugins=../plugins
/opt/slideio-viewer/lib/                    libQt6*.so.6, libslideio*.so
/opt/slideio-viewer/plugins/                platforms, xcbglintegrations, styles, imageformats
/usr/bin/slideio-viewer                     symlink
/usr/share/applications/slideio-viewer.desktop
/usr/share/icons/hicolor/256x256/apps/slideio-viewer.png   (from resources/icons/app.png)
```

The two Linux generators need different prefixes, and `CPACK_PACKAGING_INSTALL_PREFIX`
is global rather than per-generator: the `.deb` has to place files at absolute
system paths (`/opt` *and* `/usr`), while the `.tar.gz` must stay relocatable with
`bin/`, `lib/`, `plugins/` at its root. Resolve this the way CPack intends, with a
`CPACK_PROJECT_CONFIG_FILE` (`cmake/CPackLinuxOptions.cmake`) that is re-evaluated
once per generator and branches on `CPACK_GENERATOR`:

```cmake
# CMakeLists.txt, the else() branch at :299
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackLinuxOptions.cmake")

# cmake/CPackLinuxOptions.cmake
if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")   # component dirs are opt/… and usr/…
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)   # slideio-viewer_0.1.0_amd64.deb
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)   # bundled libs must not become deps
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
else()
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")   # flat tree, no opt/ or usr/ prefix
endif()
```

Under `flat` on Linux the install destinations are therefore written relative —
`opt/slideio-viewer/bin`, `usr/share/applications` — for the `.deb`, and the TGZ
branch stages only the `opt/slideio-viewer` subtree, so the tarball unpacks to the
same `bin/`, `lib/`, `plugins/` shape the other platforms use.

`CPACK_DEBIAN_PACKAGE_DEPENDS` names the libraries Qt still needs from the
system even with Qt bundled — the xcb platform plugin's dependencies. The exact
list is derived during implementation by running `ldd` over the bundled
`libqxcb.so` on the runner and keeping what is not in `lib/`; the expected shape
is `libc6, libstdc++6, libgl1, libglx-mesa0, libxkbcommon0, libxkbcommon-x11-0,
libfontconfig1, libfreetype6, libdbus-1-3` plus several `libxcb-*`. Guessing
this list is how a `.deb` installs and then fails to start, so §7's smoke test is
what certifies it.

`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` stays off deliberately: with it on, `dpkg` would
scan the bundled Qt libraries and add dependencies on the distribution's own Qt
packages, which is the opposite of bundling.

## 4. `package.sh` / `package-template.sh`

Siblings of `build.sh` / `build-template.sh`, with the same `OS_NAME` detection
and the same `BUILD_TYPE` convention:

1. Run the existing build with `-DSLIDEIO_VIEWER_INSTALL_LAYOUT=flat` and a clean
   `CMAKE_INSTALL_PREFIX` under `build/stage`.
2. `cpack -B build/packages -C "$BUILD_TYPE"` with the generators the platform
   branch of `CMakeLists.txt` already selects.
3. Leave only the packages in `build/packages`.

Point 3 is a lesson taken from SlideIO's release workflow, which documents
`cpack -B` leaving a `_CPack_Packages` staging directory — a full second copy of
the install tree — that an `upload-artifact` glob then swept into the release.
The upload step globs `*.deb`, `*.dmg`, `*.exe`, `*.zip`, `*.tar.gz` rather than
`build/packages/*`.

`CPACK_PACKAGE_FILE_NAME` is set per platform to
`slideio-viewer-<version>-{windows-x86_64,macos-arm64,linux-x86_64}` so the
artifacts are self-describing; the `.deb` keeps the Debian-native name that
`DEB-DEFAULT` produces.

## 5. Version source

`check-version` reads the version from the one place that defines it,
`CMakeLists.txt:2`:

```
project(slideio-viewer VERSION 0.1.0 LANGUAGES CXX)
```

with `sed -n 's/^project(slideio-viewer VERSION \([0-9.]*\).*/\1/p'`, and fails
the workflow when a `v*` tag disagrees. Three platforms spending an hour
producing packages whose names are wrong is the failure this prevents.

This is also already the version the About dialog shows, through
`cmake/BuildInfo.cmake`, so a release artifact and its About box cannot disagree.

## 6. Workflows

### 6.1 `build-validation.yml`

Triggers: `push` and `pull_request` on `main`, plus `workflow_dispatch`.
Concurrency group per workflow and ref, with `cancel-in-progress: true`.

Matrix over `windows-2022`, `ubuntu-22.04`, `macos-14`. Steps: checkout with
`submodules: recursive`; `setup-python`; `pip install conan`; MSVC setup on
Windows via `ilammy/msvc-dev-cmd@v1`; Linux build dependencies via `apt`; the two
caches from §8; `./build.sh`; then `ctest`.

The test directory is not the same on every platform, and `build-template.sh` is
the authority on it: Windows uses the Visual Studio multi-config generator with a
flat `build/build`, while macOS and Linux use Unix Makefiles with a per-config
`build/build/Release`. The workflow therefore runs
`ctest --test-dir build/build -C Release --output-on-failure` on Windows and
`ctest --test-dir build/build/Release --output-on-failure` elsewhere, rather than
one line that silently tests nothing on two of the three runners.

`tests/CMakeLists.txt` already puts the Qt and SlideIO bin directories on `PATH`
for `ui-tests` on Windows. On Linux and macOS the test binaries resolve their
libraries through the build tree's RPATH; if that proves insufficient on the
runners, the fix is an `ENVIRONMENT_MODIFICATION` for `LD_LIBRARY_PATH` /
`DYLD_LIBRARY_PATH` in that same file rather than an export in the workflow, so
`ctest` keeps working locally too.

The current suites need no display: nothing in `core-tests`, `infra-tests` or
`ui-tests` constructs a `QGuiApplication` or a widget.

### 6.2 `release.yml`

Triggers: `push` on `v*` tags, and `workflow_dispatch` with a `platforms` choice
(`all|windows|debian|macos`) and a `skip_tests` boolean.

Jobs:

- **`check-version`** — §5, and assembles the build matrix so a manual run can
  build one platform. Outputs `version` and `matrix`.
- **`build`** — `needs: check-version`, `fail-fast: false` so one platform's
  failure does not hide whether the others package cleanly. Same setup steps as
  build-validation, then `ctest`, then `./package.sh`, then the §7 smoke test,
  then `upload-artifact` with `if-no-files-found: error`.
- **`publish`** — `needs: [check-version, build]`, gated on
  `github.event_name == 'push' && github.ref_type == 'tag'`, with
  `permissions: contents: write`. Downloads every artifact with
  `merge-multiple: true` and creates a **draft** release via
  `softprops/action-gh-release@v2` with `fail_on_unmatched_files: true`.

Both halves of the publish condition are load-bearing, for the reason SlideIO's
workflow documents: the dispatch API accepts a tag ref, so `ref_type == 'tag'`
alone would let a manual run that selected one platform and set `skip_tests`
publish a release. Requiring `event_name == 'push'` makes "a manual run cannot
publish" true rather than merely usually true.

## 7. Smoke tests

Each runs against the installed or unpacked artifact, with no reference to the
build tree, no Conan and no toolchain file.

**Debian** — `sudo apt-get install -y ./build/packages/*.deb`; assert
`ldd /opt/slideio-viewer/bin/slideio-viewer` reports no `not found`; assert the
platform plugin `plugins/platforms/libqxcb.so` is present and that *its* `ldd` is
also clean (this is what certifies the dependency list in §3); then start
`/usr/bin/slideio-viewer -platform offscreen` under `timeout 15` and require it
to still be running when the timeout fires.

**macOS** — `hdiutil attach` the `.dmg`, copy the `.app` out, `hdiutil detach`;
assert `otool -L` on the executable and on every bundled dylib names no path
under `/Users/runner` or a Conan cache — an absolute build-machine path is
exactly the breakage the Windows glob represents, and it is invisible until
someone else runs the app; assert `Contents/PlugIns/platforms/libqcocoa.dylib`
exists; then launch with `-platform offscreen` under `timeout 15`.

**Windows** — `Expand-Archive` the `.zip`; assert `Qt6Core.dll`,
`Qt6Widgets.dll`, `Qt6OpenGLWidgets.dll` and `plugins/platforms/qwindows.dll` are
present, which is the direct regression test for the `OPTIONAL` installs of §1;
then `Start-Process` the executable with `-platform offscreen`, sleep, and
require `HasExited` to be false before stopping it.

A GUI process that stays up has resolved every library it needs to reach the
event loop. One that exits immediately has not, and that is the failure these
catch. Exit-code conventions differ per platform (`timeout` reports 124), so each
step asserts on "still running", never on a zero exit.

## 8. Caching

Two caches per platform:

```yaml
- uses: actions/cache@v4            # Conan packages
  with:
    path: ~/.conan2
    key: conan-${{ matrix.os }}-${{ hashFiles('conanfile.py', 'conan/profiles/**') }}
    restore-keys: conan-${{ matrix.os }}-

- uses: actions/cache@v4            # built SlideIO install tree
  with:
    path: extern/slideio/build/install
    key: slideio-${{ matrix.os }}-${{ steps.slideio_rev.outputs.sha }}
```

The SlideIO cache is the one that matters: `build-template.sh` already skips the
submodule build when `$SLIDEIO_ROOT/release/include/slideio/slideio/slideio.hpp`
exists, so a restored cache turns a long build into a no-op without any change to
the script. The key is the submodule SHA, read with
`git -C extern/slideio rev-parse HEAD` in a prior step, so moving the pin
invalidates the cache exactly when it should.

**Risk, called out rather than assumed away:** the first run on each platform
builds SlideIO *and* possibly Qt from source. Conan Center may not publish a
binary for `qt/6.7.3` with `shared=True, qtshadertools=True, opengl=desktop`, in
which case `--build=missing` compiles Qt, which can approach or exceed the
six-hour job limit. If the first run hits that, the mitigations in preference
order are: relax the option set to one Conan Center builds for; or seed the Conan
cache from a separate manually dispatched warm-up job whose only purpose is to
populate it. This is the single largest schedule risk in the plan and the first
thing to measure.

## 9. Runners and the toolchain floor

- **`ubuntu-22.04`**, not `ubuntu-24.04`: it fixes the glibc floor at 2.35, so
  the `.deb` installs on Ubuntu 22.04+ and Debian 12+. SlideIO's release workflow
  pins 22.04 for the same stated reason. Built on 24.04 it would require glibc
  2.39 and refuse Debian 12.
- **`conan/profiles/Linux/release` pins `compiler.version=14`**, which the 22.04
  image does not carry. Add `conan/profiles/Linux/release-ci` pinned to the gcc
  the image actually provides, and have the Linux workflow pass it through the
  existing `CONAN_PROFIlE` environment variable that `build-template.sh` already
  honours. That name is misspelled in the script — a lowercase `l` in `PROFIlE` —
  so the workflow must spell it the same way; correcting it is a separate change
  rather than something to slip into this one. The exact gcc version is read off
  the runner during implementation. A
  separate profile means CI and local builds resolve different Conan package IDs
  and therefore different caches, which is expected rather than a problem.
- **`macos-14`**, the first arm64 image, named explicitly rather than
  `macos-latest` so the artifact's architecture cannot change under us. It
  matches `conan/profiles/Mac/release`, which already pins `arch=armv8`.
- **`windows-2022`**, matching `compiler.version=194` (VS 2022) in
  `conan/profiles/Windows/release`.

Note that the profile directory name is `Mac`, which is what `build-template.sh`
derives from `uname -s`; the unused `conan/profiles/OSX/` directory is left alone.

## 10. Acceptance

- Both workflow files pass `actionlint`.
- A `workflow_dispatch` run of `release.yml` for each platform produces its
  artifact and passes its smoke test, without publishing anything.
- A `.deb` installed in a clean Ubuntu 22.04 container starts under
  `-platform offscreen`.
- `./package.sh` on a developer machine produces the same artifacts CI does,
  which is the property that keeps the packaging rules reviewable.
- `build-validation.yml` is green on all three platforms with `ctest` passing.

## 11. Work not in this change, noted

- `cmake/CopySlideIODlls.cmake` copies SlideIO DLLs into the build tree on
  Windows only. The build-tree executable still will not start on Linux or macOS
  without the install step; CI uses the installed tree throughout, so this is not
  on the critical path, but it is the reason `ctest` is the validation gate
  rather than launching the application.
- The `.desktop` file and the `/usr/bin` symlink are new files with no equivalent
  on the other platforms; they exist only in the Linux branch.
