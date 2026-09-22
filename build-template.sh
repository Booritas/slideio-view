#!/usr/bin/env bash
set -e

# OS detection (override via OS_NAME env var)
if [ -z "${OS_NAME:-}" ]; then
    case "$(uname -s)" in
        Darwin)               OS_NAME=Mac ;;
        Linux)                OS_NAME=Linux ;;
        MINGW*|MSYS*|CYGWIN*) OS_NAME=Windows ;;
        *) echo "Unsupported OS: $(uname -s)" >&2; exit 1 ;;
    esac
fi

BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')

# Conan profile path (override via CONAN_PROFIlE env var)
: "${CONAN_PROFIlE:=conan/profiles/${OS_NAME}/${BUILD_TYPE_LOWER}}"

# Generator/layout/path defaults per platform.
# Windows uses VS multi-config (flat build dir, --config required).
# Mac/Linux use single-config Unix Makefiles (per-config build dir, no --config).
if [ "$OS_NAME" = "Windows" ]; then
    GENERATOR="Visual Studio 17 2022"
    BUILD_DIR="build/build"
    TOOLCHAIN="build/build/generators/conan_toolchain.cmake"
    CONFIG_FLAG=("--config" "$BUILD_TYPE")
    : "${PYTHON:=python}"
else
    GENERATOR="Unix Makefiles"
    BUILD_DIR="build/build/$BUILD_TYPE"
    TOOLCHAIN="$BUILD_DIR/generators/conan_toolchain.cmake"
    CONFIG_FLAG=()
    : "${PYTHON:=python3}"
fi

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
SLIDEIO_DIR="$REPO_ROOT/extern/slideio"

# SlideIO ships as a git submodule (extern/slideio) and is built from source
# into extern/slideio/build/install, whose release/ and debug/ subdirectories
# are the layout FindSlideIO.cmake expects. Setting SLIDEIO_ROOT in the
# environment points the build at an install tree maintained elsewhere and
# skips the submodule build entirely.
if [ -n "${SLIDEIO_ROOT:-}" ]; then
    echo "Using external SlideIO install: $SLIDEIO_ROOT"
else
    SLIDEIO_ROOT="$SLIDEIO_DIR/build/install"

    # Initialise the submodule only when it has never been checked out. An
    # existing checkout is deliberately left alone: "submodule update" resets it
    # to the pinned revision, which would detach work in progress inside
    # extern/slideio out from under whoever is doing it, and build sources other
    # than the ones they are looking at. Report the drift instead.
    if [ ! -f "$SLIDEIO_DIR/CMakeLists.txt" ]; then
        git -C "$REPO_ROOT" submodule update --init --recursive
    else
        _pinned=$(git -C "$REPO_ROOT" ls-tree HEAD -- extern/slideio | awk '{print $3}')
        _actual=$(git -C "$SLIDEIO_DIR" rev-parse HEAD 2>/dev/null || true)
        if [ -n "$_pinned" ] && [ -n "$_actual" ] && [ "$_pinned" != "$_actual" ]; then
            echo "WARNING: extern/slideio is checked out at $_actual," >&2
            echo "         but this commit pins $_pinned." >&2
            echo "         Run 'git submodule update --init --recursive' and rebuild with" >&2
            echo "         SLIDEIO_REBUILD=1 to build the pinned revision." >&2
        fi
    fi

    # FindSlideIO.cmake names the release library in its REQUIRED_VARS, so a
    # release install has to exist even for a debug build; a debug build needs
    # the debug install on top of it. install.py knows only these two names --
    # every other CMake config links against the release install.
    SLIDEIO_CONFIGS=(release)
    if [ "$BUILD_TYPE" = "Debug" ]; then
        SLIDEIO_CONFIGS+=(debug)
    fi

    # Building SlideIO is slow, so each configuration is built only when its
    # install tree is missing. Set SLIDEIO_REBUILD=1 to force a rebuild, e.g.
    # after moving the submodule to a new revision.
    for _config in "${SLIDEIO_CONFIGS[@]}"; do
        if [ -f "$SLIDEIO_ROOT/$_config/include/slideio/slideio/slideio.hpp" ] \
           && [ "${SLIDEIO_REBUILD:-0}" = "0" ]; then
            echo "SlideIO ($_config) already installed in $SLIDEIO_ROOT/$_config"
            continue
        fi
        echo "Building SlideIO ($_config) from extern/slideio..."
        (cd "$SLIDEIO_DIR" && "$PYTHON" install.py -a install -c "$_config" \
            -bd build/build -pr build/install)
    done
fi

conan install . --output-folder=build --build=missing \
    -s build_type="$BUILD_TYPE" -s compiler.cppstd=17 \
    -pr:b "$CONAN_PROFIlE" -pr:h "$CONAN_PROFIlE"

cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
    -DSLIDEIO_ROOT="$SLIDEIO_ROOT"

cmake --build "$BUILD_DIR" "${CONFIG_FLAG[@]}"
cmake --install "$BUILD_DIR" "${CONFIG_FLAG[@]}"
