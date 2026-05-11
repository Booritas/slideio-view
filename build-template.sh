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
    DEFAULT_SLIDEIO_ROOT="D:/Projects/slideio/slideio/build/install"
else
    GENERATOR="Unix Makefiles"
    BUILD_DIR="build/build/$BUILD_TYPE"
    TOOLCHAIN="$BUILD_DIR/generators/conan_toolchain.cmake"
    CONFIG_FLAG=()
    DEFAULT_SLIDEIO_ROOT="$HOME/projects/slideio/slideio/build/install"
fi

# SlideIO install root (override via SLIDEIO_ROOT env var).
# FindSlideIO.cmake appends release/ and debug/ subdirs itself.
: "${SLIDEIO_ROOT:=$DEFAULT_SLIDEIO_ROOT}"

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
