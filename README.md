# SlideIO Viewer

A cross-platform C++ desktop application for viewing and navigating digital pathology whole-slide images (WSI).

SlideIO Viewer renders gigapixel-scale slide images with smooth pan and zoom using GPU-accelerated tile rendering. It supports all major pathology slide formats through the [SlideIO](https://github.com/Booritas/slideio) library.

## Supported Formats

SVS, NDPI, SCN, CZI, ZVI, VSI, AFI, MRXS, BIF, TIFF/BigTIFF, OME-TIFF, DICOM WSI, and all other formats supported by SlideIO.

## Features

- GPU-accelerated tile rendering (OpenGL 3.3+)
- Multi-resolution pyramid navigation with async tile loading
- LRU tile cache with configurable memory budget
- Minimap overview and zoom indicator
- Drag-and-drop file opening
- Recent files menu
- Dark theme UI

## Requirements

- C++17 compiler (MSVC 2022, GCC 12+, or AppleClang 14+)
- CMake 3.20+
- Conan 2 package manager
- Qt 6.5+
- SlideIO library (pre-built)
- OpenGL 3.3+ capable GPU

## Building

### 1. Install dependencies with Conan

```bash
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=17
```

### 2. Configure with CMake

```bash
cmake -S . -B build/build -G "Visual Studio 17 2022" \
  -DCMAKE_TOOLCHAIN_FILE=build/build/generators/conan_toolchain.cmake \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
  -DSLIDEIO_ROOT="path/to/slideio/install"
```

On Linux/macOS with Ninja:

```bash
cmake -S . -B build/build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=build/build/generators/conan_toolchain.cmake \
  -DSLIDEIO_ROOT="path/to/slideio/install"
```

The `SLIDEIO_ROOT` should point to a directory containing `release/` and `debug/` subdirectories with SlideIO headers, libraries, and DLLs.

### 3. Build

```bash
cmake --build build/build --config Release
```

### 4. Run tests

```bash
cd build/build
ctest --output-on-failure -C Release
```

## Installation

### Install to a directory

```bash
cmake --install build/build --config Release --prefix "path/to/install"
```

This produces a self-contained directory with the executable, all required DLLs (Qt, SlideIO), and Qt plugins. No environment setup needed.

### Build a distributable package

```bash
cd build/build
cpack -G ZIP -C Release    # Portable ZIP archive
cpack -G NSIS -C Release   # Windows installer (requires NSIS)
```

## Usage

```bash
slideio-viewer                          # Launch empty
slideio-viewer path/to/slide.svs        # Open a slide directly
```

### Navigation

| Input | Action |
|-------|--------|
| Scroll wheel | Zoom toward cursor |
| Left-click drag | Pan |
| Arrow keys | Pan (20px per press) |
| Ctrl+0 | Fit to window |
| Ctrl+1 | Actual pixels (1:1) |
| Ctrl++ / Ctrl+- | Zoom in / out |
| F11 | Full screen |
| Ctrl+M | Toggle minimap |

### File operations

| Shortcut | Action |
|----------|--------|
| Ctrl+O | Open slide |
| Ctrl+W | Close slide |
| Drag & drop | Open dropped file |

## Project Structure

```
src/
  core/     Pure C++17 domain layer (no external dependencies)
  app/      Application services
  infra/    SlideIO adapter, tile cache, tile scheduler
  ui/       Qt widgets, OpenGL rendering, main window
  main.cpp  Entry point
tests/
  core/     Unit tests for domain types and coordinate math
  infra/    Unit tests for tile cache
```

The project uses a layered architecture with strict downward dependencies enforced at the CMake level. See [CLAUDE.md](CLAUDE.md) for architecture details.

## License

BSD 3-Clause License. See [LICENSE](LICENSE).
