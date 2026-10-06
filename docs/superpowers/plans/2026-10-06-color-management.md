# Colour Management Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the viewer convert slide pixels to sRGB through the slide's ICC profile, under a checkable menu item that is off until the user asks for it.

**Architecture:** `SlideIOAdapter` holds the origin scene and a `transformScene`-wrapped copy, selecting between them per read. The active colour mode is part of `TileKey`, so raw and managed tiles are distinct cache entries — which removes the in-flight-read race and keeps both renditions cached. Capability is probed once at slide open by attempting the wrap.

**Tech Stack:** C++17, Qt 6 Widgets, SlideIO (`slideio`, `slideio-core`, `slideio-transformer`), Catch2, CMake 3.20+, spdlog.

**Spec:** `docs/superpowers/specs/2026-10-06-color-management-design.md`

## Global Constraints

- C++17. 4-space indent, 120-column limit. Allman braces for classes and functions, K&R for control flow. `#pragma once`.
- Naming: `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, `lowercase` namespaces, `PascalCase.h/.cpp` files.
- Layer rule: `slideio-viewer-core` links **nothing** — no Qt, no SlideIO. SlideIO headers stay inside `src/infra/src/*.cpp`; `SlideIOAdapter.h` forward-declares `slideio::Slide` and `slideio::Scene` and must continue to.
- American spelling in user-facing strings and identifiers (`color`, not `colour`), matching the existing codebase. British spelling is fine in prose comments.
- Colour target is `ColorTarget::sRGB` only. Rendering intent and black-point compensation stay at SlideIO defaults and are not exposed.
- `MissingProfilePolicy::Fail`, never `AssumeSRGB` — see spec §5.
- Commit messages: imperative, sentence case, no `feat:`/`fix:` prefixes (match `git log`).
- Build: `export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"` before any cmake/ctest invocation.

## Review Focus

Five conditions the spec implies that no task's happy path exercises. Each has a test assigned to the task that owns the code.

1. **3-channel 8-bit fluorescence slide** — passes SlideIO's own bind check and would be ICC-transformed as if intensities were R/G/B. Must report `NotColorimetric` and never reach `transformScene`. *(Task 2, Step 1 — the pure gate; Task 4, Step 1 — against a real CZI)*
2. **16-bit RGB brightfield** — `SlideInfo::isBrightfield` is false for it, but `ColorManagement` supports it. Must report `Available`. *(Task 2, Step 1 — no 16-bit RGB fixture exists in the corpus, which is why the gate is a pure function)*
3. **Toggle while tiles are in flight** — a worker already inside `readTile` must not be able to land a stale-mode tile that later renders. *(Task 1, Step 1 — guaranteed by key inequality)*
4. **Default profile configured AND slide embeds its own** — the embedded profile must win; SlideIO would let the override displace it. *(Task 4, Step 7)*
5. **Configured default profile path unreadable at slide open** — must degrade to `NoProfile` with a logged warning, never crash and never claim to be colour-managed. *(Task 7, Step 7)*

---

### Task 1: Colour mode on `TileKey`

Makes a raw tile and a managed tile of the same region distinct cache entries. This is what removes the toggle race, so it lands first and alone.

**Files:**
- Modify: `src/core/include/slideio/viewer/core/TileKey.h`
- Modify: `src/core/src/TileKey.cpp`
- Test: `tests/core/TileKeyTest.cpp` (existing file, append)

**Interfaces:**
- Consumes: nothing.
- Produces: `slideio::viewer::core::ColorMode` (`Raw`, `Managed`); `TileKey(int level, int column, int row, int zIndex = 0, int tFrame = 0, ColorMode colorMode = ColorMode::Raw)`; `ColorMode TileKey::colorMode() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/TileKeyTest.cpp`:

```cpp
TEST_CASE("TileKey defaults to the raw colour mode", "[core][TileKey]")
{
    TileKey key(2, 3, 4);
    REQUIRE(key.colorMode() == ColorMode::Raw);
}

TEST_CASE("TileKeys differing only in colour mode are not equal", "[core][TileKey]")
{
    // This inequality is what keeps a tile read in the old mode from being
    // served after the user toggles: the two renditions are separate cache
    // entries, so a late raw read lands under a key managed rendering never
    // looks up.
    TileKey raw(1, 2, 3, 0, 0, ColorMode::Raw);
    TileKey managed(1, 2, 3, 0, 0, ColorMode::Managed);
    REQUIRE(raw != managed);
    REQUIRE_FALSE(raw == managed);
}

TEST_CASE("TileKeys agreeing on colour mode and coordinates are equal", "[core][TileKey]")
{
    TileKey a(1, 2, 3, 4, 5, ColorMode::Managed);
    TileKey b(1, 2, 3, 4, 5, ColorMode::Managed);
    REQUIRE(a == b);
    REQUIRE(std::hash<TileKey>{}(a) == std::hash<TileKey>{}(b));
}

TEST_CASE("TileKeys differing only in colour mode hash apart", "[core][TileKey]")
{
    TileKey raw(7, 8, 9, 0, 0, ColorMode::Raw);
    TileKey managed(7, 8, 9, 0, 0, ColorMode::Managed);
    REQUIRE(std::hash<TileKey>{}(raw) != std::hash<TileKey>{}(managed));
}

TEST_CASE("TileKey::toString names the colour mode only when managed", "[core][TileKey]")
{
    // Raw is the overwhelmingly common case; naming it on every log line
    // would be noise. Managed is the one worth seeing.
    REQUIRE(TileKey(1, 2, 3).toString().find("managed") == std::string::npos);
    REQUIRE(TileKey(1, 2, 3, 0, 0, ColorMode::Managed).toString().find("managed")
            != std::string::npos);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
cmake --build build/build --config Release --target slideio-viewer-core-tests
```

Expected: compile failure, `'ColorMode': undeclared identifier` and `'colorMode': is not a member of 'TileKey'`.

- [ ] **Step 3: Add the enum, field and accessor to the header**

In `src/core/include/slideio/viewer/core/TileKey.h`, above `class TileKey`:

```cpp
/// Whether a tile holds the scene's pixels as decoded, or converted to sRGB
/// through its ICC profile. Part of the key because the two are different
/// pixel data for the same region: keeping them apart lets both stay cached
/// and stops a read issued before a toggle from being served after it.
enum class ColorMode { Raw, Managed };
```

Then in the class, replace the constructor declaration and add the accessor:

```cpp
    TileKey(int level, int column, int row, int zIndex = 0, int tFrame = 0,
            ColorMode colorMode = ColorMode::Raw);

    int tFrame() const;
    ColorMode colorMode() const;
```

Add the member alongside the others:

```cpp
    ColorMode m_colorMode;
```

Extend the hash specialisation at the bottom of the file, following the existing combine pattern:

```cpp
        seed ^= hash<int>{}(static_cast<int>(key.colorMode())) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
```

- [ ] **Step 4: Implement in the source file**

In `src/core/src/TileKey.cpp`, add `m_colorMode` to both constructors' initialiser lists (`ColorMode::Raw` in the default one, `colorMode` in the other), then add:

```cpp
ColorMode TileKey::colorMode() const
{
    return m_colorMode;
}
```

Add the field to `operator==`:

```cpp
        && m_tFrame == other.m_tFrame
        && m_colorMode == other.m_colorMode;
```

And to `toString()`, before the closing paren:

```cpp
    if (m_colorMode == ColorMode::Managed) {
        s += ", managed";
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
./build/build/tests/Release/slideio-viewer-core-tests.exe "[TileKey]"
```

Expected: all pass.

- [ ] **Step 6: Run the whole suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: 3/3 pass. `TilePyramid::visibleTiles` calls `emplace_back(level, c, r)` and `tileKeyAt` returns `TileKey(level, col, row)`; both compile unchanged against the defaulted parameter and produce `Raw`, which is correct — the UI re-stamps the mode in Task 6.

- [ ] **Step 7: Commit**

```bash
git add src/core/include/slideio/viewer/core/TileKey.h src/core/src/TileKey.cpp tests/core/TileKeyTest.cpp
git commit -m "Make the colour mode part of a tile's identity"
```

---

### Task 2: Availability reasons and ICC header inspection

Two pure helpers the UI needs: the text for a disabled menu item, and a sanity check so choosing a bad profile fails at the file dialog rather than silently degrading later.

**Files:**
- Create: `src/core/include/slideio/viewer/core/ColorManagement.h`
- Create: `src/core/src/ColorManagement.cpp`
- Modify: `src/core/CMakeLists.txt`
- Create: `tests/core/ColorManagementTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `ColorManagementAvailability` (`Available`, `NotColorimetric`, `NoProfile`, `BindFailed`); `std::string colorManagementUnavailableReason(ColorManagementAvailability, const std::string& detail)`; `struct IccHeaderSummary { bool plausible; std::string dataSpace; size_t declaredSize; }`; `IccHeaderSummary inspectIccHeader(const std::vector<uint8_t>&)`; `bool isColorimetricForIcc(int numChannels, DataType dataType, bool fluorescenceHint)`.

`isColorimetricForIcc` is the gate that decides whether ICC conversion means anything for a scene. It lives here, as a pure function, rather than inside the adapter, because the two cases that matter most — a 3-channel 8-bit fluorescence image, and 16-bit RGB brightfield — are then testable without needing a fixture slide of each kind in the corpus.

- [ ] **Step 1: Write the failing tests**

Create `tests/core/ColorManagementTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/ColorManagement.h"

#include <cstring>

using namespace slideio::viewer::core;

namespace
{

// A minimal but structurally valid ICC v4 header. Only the fields
// inspectIccHeader reads are filled: size at 0, data colour space at 16,
// and the 'acsp' signature at 36. Everything else stays zero, which is
// exactly the shape of input the function has to tolerate.
std::vector<uint8_t> makeIccHeader(const char* dataSpace = "RGB ", uint32_t declaredSize = 128)
{
    std::vector<uint8_t> bytes(128, 0);
    bytes[0] = static_cast<uint8_t>((declaredSize >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((declaredSize >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((declaredSize >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(declaredSize & 0xFF);
    std::memcpy(bytes.data() + 16, dataSpace, 4);
    std::memcpy(bytes.data() + 36, "acsp", 4);
    return bytes;
}

} // namespace

TEST_CASE("inspectIccHeader accepts a well-formed RGB profile header", "[core][ColorManagement]")
{
    const IccHeaderSummary s = inspectIccHeader(makeIccHeader());
    REQUIRE(s.plausible);
    REQUIRE(s.dataSpace == "RGB ");
    REQUIRE(s.declaredSize == 128);
}

TEST_CASE("inspectIccHeader reports the data space of a non-RGB profile", "[core][ColorManagement]")
{
    // Reported rather than rejected: the caller decides. ColorManagement
    // only binds RGB, but inspectIccHeader is not the place that knows that.
    const IccHeaderSummary s = inspectIccHeader(makeIccHeader("CMYK"));
    REQUIRE(s.plausible);
    REQUIRE(s.dataSpace == "CMYK");
}

TEST_CASE("inspectIccHeader rejects a buffer shorter than the ICC header", "[core][ColorManagement]")
{
    std::vector<uint8_t> truncated = makeIccHeader();
    truncated.resize(127);
    REQUIRE_FALSE(inspectIccHeader(truncated).plausible);
}

TEST_CASE("inspectIccHeader rejects empty input", "[core][ColorManagement]")
{
    REQUIRE_FALSE(inspectIccHeader({}).plausible);
}

TEST_CASE("inspectIccHeader rejects a buffer without the acsp signature", "[core][ColorManagement]")
{
    std::vector<uint8_t> notIcc = makeIccHeader();
    notIcc[36] = 'x';
    REQUIRE_FALSE(inspectIccHeader(notIcc).plausible);
}

TEST_CASE("inspectIccHeader rejects a header claiming more bytes than it has",
          "[core][ColorManagement]")
{
    // A truncated download is the realistic way this happens.
    const IccHeaderSummary s = inspectIccHeader(makeIccHeader("RGB ", 4096));
    REQUIRE_FALSE(s.plausible);
}

TEST_CASE("colorManagementUnavailableReason explains a non-colorimetric slide",
          "[core][ColorManagement]")
{
    const std::string r =
        colorManagementUnavailableReason(ColorManagementAvailability::NotColorimetric, "");
    REQUIRE(r.find("RGB brightfield") != std::string::npos);
}

TEST_CASE("colorManagementUnavailableReason explains a slide with no profile",
          "[core][ColorManagement]")
{
    const std::string r =
        colorManagementUnavailableReason(ColorManagementAvailability::NoProfile, "");
    REQUIRE(r.find("no ICC profile") != std::string::npos);
}

TEST_CASE("colorManagementUnavailableReason passes through SlideIO's own message",
          "[core][ColorManagement]")
{
    // SlideIO's bind errors are specific ("the source profile describes CMYK
    // data, not RGB"). Reproducing that judgement here would only let the two
    // drift, so the detail is surfaced verbatim.
    const std::string r = colorManagementUnavailableReason(
        ColorManagementAvailability::BindFailed, "the source profile describes CMYK data, not RGB");
    REQUIRE(r.find("CMYK") != std::string::npos);
}

TEST_CASE("colorManagementUnavailableReason is empty when colour management is available",
          "[core][ColorManagement]")
{
    REQUIRE(colorManagementUnavailableReason(ColorManagementAvailability::Available, "").empty());
}

TEST_CASE("isColorimetricForIcc accepts 8-bit RGB brightfield", "[core][ColorManagement]")
{
    REQUIRE(isColorimetricForIcc(3, DataType::Byte, false));
}

TEST_CASE("isColorimetricForIcc accepts 16-bit RGB brightfield", "[core][ColorManagement]")
{
    // Review Focus 2. SlideInfo::isBrightfield is false for this case -- it
    // only admits 3-channel Byte -- but ColorManagement binds DT_UInt16 just
    // as happily, so gating on isBrightfield would deny colour management to
    // slides that can have it.
    REQUIRE(isColorimetricForIcc(3, DataType::UInt16, false));
}

TEST_CASE("isColorimetricForIcc rejects three fluorescence channels", "[core][ColorManagement]")
{
    // Review Focus 1, and the one case where being wrong is silently wrong.
    // Three 8-bit fluorescence channels satisfy every check SlideIO itself
    // makes -- it counts channels and inspects data types, which cannot reveal
    // that the values are intensities rather than colour. Binding would ICC
    // transform them as though they were R/G/B and produce confident nonsense.
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::Byte, true));
}

TEST_CASE("isColorimetricForIcc rejects channel counts other than three",
          "[core][ColorManagement]")
{
    REQUIRE_FALSE(isColorimetricForIcc(1, DataType::Byte, false));
    REQUIRE_FALSE(isColorimetricForIcc(4, DataType::Byte, false));
    REQUIRE_FALSE(isColorimetricForIcc(0, DataType::Byte, false));
}

TEST_CASE("isColorimetricForIcc rejects data types ColorManagement cannot bind",
          "[core][ColorManagement]")
{
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::Float32, false));
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::Int16, false));
    REQUIRE_FALSE(isColorimetricForIcc(3, DataType::None, false));
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

In `tests/CMakeLists.txt`, add to the `slideio-viewer-core-tests` source list, after `core/ColorProfileInfoTest.cpp`:

```cmake
    core/ColorManagementTest.cpp
```

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
```

Expected: `Cannot open include file: 'slideio/viewer/core/ColorManagement.h'`.

- [ ] **Step 3: Write the header**

The availability enum goes in `Types.h` beside the other core enums, **not** in the new header. `isColorimetricForIcc` needs `DataType` from `Types.h`, and `SlideInfo` needs the availability enum; putting the enum in `ColorManagement.h` would make the two headers include each other. Add to `src/core/include/slideio/viewer/core/Types.h`, after the `RenderingIntent` enum added by `54ac47a`:

```cpp
/// Why colour management is or is not offered for a slide.
enum class ColorManagementAvailability
{
    Available,
    /// Not three channels of Byte/UInt16, or the channels are fluorescence.
    NotColorimetric,
    /// Colorimetric, but the slide embeds no profile and no default is configured.
    NoProfile,
    /// SlideIO refused the wrap; the accompanying detail carries its message.
    BindFailed,
};
```

Then create `src/core/include/slideio/viewer/core/ColorManagement.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// Whether ICC conversion means anything for a scene of this shape.
///
/// Stricter than the check ColorManagement::bindToSource makes, deliberately.
/// That one counts channels and inspects data types, which three 8-bit
/// fluorescence channels satisfy -- it has no way to know the values are
/// intensities rather than colour, and would transform them as though they
/// were R/G/B. Only the viewer holds that fact, so only the viewer can refuse.
///
/// Also note this is not SlideInfo::isBrightfield, which is false for 16-bit
/// RGB brightfield -- a case ColorManagement supports.
bool isColorimetricForIcc(int numChannels, DataType dataType, bool fluorescenceHint);

/// Tooltip text for a disabled "Color management" menu item. Empty when
/// availability is Available.
std::string colorManagementUnavailableReason(ColorManagementAvailability availability,
                                             const std::string& detail);

/// The fields of an ICC profile header this application reads.
struct IccHeaderSummary
{
    bool plausible = false;     ///< parses as an ICC profile header
    std::string dataSpace;      ///< 4-character signature, e.g. "RGB ", "CMYK"
    size_t declaredSize = 0;    ///< profile size the header claims
};

/// Sanity-check a candidate ICC profile, per ICC.1:2010 section 7.2.
///
/// This exists so that choosing a default profile fails at the file dialog,
/// naming the problem, rather than degrading silently to an assumed sRGB
/// several slides later. It is not a parser: lcms2, inside SlideIO, remains
/// the authority on whether a profile is usable.
IccHeaderSummary inspectIccHeader(const std::vector<uint8_t>& bytes);

} // namespace slideio::viewer::core
```

- [ ] **Step 4: Write the implementation**

Create `src/core/src/ColorManagement.cpp`:

```cpp
#include "slideio/viewer/core/ColorManagement.h"

namespace slideio::viewer::core
{

namespace
{

constexpr size_t kIccHeaderSize = 128;
constexpr size_t kIccSizeOffset = 0;
constexpr size_t kIccDataSpaceOffset = 16;
constexpr size_t kIccSignatureOffset = 36;

uint32_t readBigEndian32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return (static_cast<uint32_t>(bytes[offset]) << 24)
         | (static_cast<uint32_t>(bytes[offset + 1]) << 16)
         | (static_cast<uint32_t>(bytes[offset + 2]) << 8)
         | static_cast<uint32_t>(bytes[offset + 3]);
}

} // namespace

bool isColorimetricForIcc(int numChannels, DataType dataType, bool fluorescenceHint)
{
    return numChannels == 3
        && (dataType == DataType::Byte || dataType == DataType::UInt16)
        && !fluorescenceHint;
}

std::string colorManagementUnavailableReason(ColorManagementAvailability availability,
                                             const std::string& detail)
{
    switch (availability) {
    case ColorManagementAvailability::Available:
        return {};
    case ColorManagementAvailability::NotColorimetric:
        return "Color management applies to RGB brightfield slides only";
    case ColorManagementAvailability::NoProfile:
        return "This slide embeds no ICC profile, and no default profile is set";
    case ColorManagementAvailability::BindFailed:
        return detail.empty() ? "This slide's color profile could not be used"
                              : "This slide's color profile could not be used: " + detail;
    }
    return {};
}

IccHeaderSummary inspectIccHeader(const std::vector<uint8_t>& bytes)
{
    IccHeaderSummary summary;
    if (bytes.size() < kIccHeaderSize) {
        return summary;
    }
    if (bytes[kIccSignatureOffset] != 'a' || bytes[kIccSignatureOffset + 1] != 'c'
        || bytes[kIccSignatureOffset + 2] != 's' || bytes[kIccSignatureOffset + 3] != 'p') {
        return summary;
    }

    const uint32_t declared = readBigEndian32(bytes, kIccSizeOffset);
    // A header claiming more bytes than the buffer holds is the signature of a
    // truncated file, which lcms2 would reject later and less legibly.
    if (declared < kIccHeaderSize || declared > bytes.size()) {
        return summary;
    }

    summary.plausible = true;
    summary.declaredSize = declared;
    summary.dataSpace.assign(bytes.begin() + kIccDataSpaceOffset,
                             bytes.begin() + kIccDataSpaceOffset + 4);
    return summary;
}

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Register the source and run the tests**

In `src/core/CMakeLists.txt`, add after `src/ColorProfileInfo.cpp`:

```cmake
    src/ColorManagement.cpp
```

```bash
cmake --build build/build --config Release --target slideio-viewer-core-tests
./build/build/tests/Release/slideio-viewer-core-tests.exe "[ColorManagement]"
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/ColorManagement.h src/core/src/ColorManagement.cpp \
        src/core/CMakeLists.txt tests/core/ColorManagementTest.cpp tests/CMakeLists.txt
git commit -m "Add colour management availability reasons and ICC header checks"
```

---

### Task 3: Link `slideio-transformer`, and carry availability on `SlideInfo`

Build plumbing plus the data fields, with no behaviour change. Keeping it separate means a link or find-module problem surfaces on its own rather than tangled with the adapter logic.

**Files:**
- Modify: `cmake/FindSlideIO.cmake`
- Modify: `src/infra/CMakeLists.txt`
- Modify: `src/core/include/slideio/viewer/core/Types.h`
- Modify: `src/infra/src/SlideIOAdapter.cpp`

**Interfaces:**
- Consumes: `ColorManagementAvailability` from Task 2.
- Produces: imported target `SlideIO::transformer`; `SlideInfo::colorManagement`, `SlideInfo::colorManagementDetail`, `SlideInfo::fluorescenceHint`.

- [ ] **Step 1: Add the transformer library lookup**

In `cmake/FindSlideIO.cmake`, beside the existing core cache unsets, add:

```cmake
unset(SlideIO_TRANSFORMER_LIBRARY_RELEASE CACHE)
unset(SlideIO_TRANSFORMER_LIBRARY_DEBUG CACHE)
```

After the `SlideIO_CORE_LIBRARY_DEBUG` `find_library` block, add:

```cmake
find_library(SlideIO_TRANSFORMER_LIBRARY_RELEASE
    NAMES slideio-transformer
    PATHS "${_slideio_release_prefix}/lib"
    NO_DEFAULT_PATH
)

find_library(SlideIO_TRANSFORMER_LIBRARY_DEBUG
    NAMES slideio-transformer_d
    PATHS "${_slideio_debug_prefix}/lib"
    NO_DEFAULT_PATH
)
```

Add `SlideIO_TRANSFORMER_LIBRARY_RELEASE` to the `REQUIRED_VARS` of `find_package_handle_standard_args`.

- [ ] **Step 2: Add the imported target**

Immediately before the `endif()` that closes the `if(SlideIO_FOUND AND NOT TARGET SlideIO::slideio)` block, add — mirroring the `SlideIO::core` block exactly, including the Windows import-lib split and the debug fallback:

```cmake
    if(WIN32)
        set(_slideio_transformer_loc_release "${_slideio_release_prefix}/bin/slideio-transformer.dll")
        set(_slideio_transformer_loc_debug   "${_slideio_debug_prefix}/bin/slideio-transformer_d.dll")
    else()
        set(_slideio_transformer_loc_release "${SlideIO_TRANSFORMER_LIBRARY_RELEASE}")
        set(_slideio_transformer_loc_debug   "${SlideIO_TRANSFORMER_LIBRARY_DEBUG}")
    endif()

    add_library(SlideIO::transformer SHARED IMPORTED)
    set_target_properties(SlideIO::transformer PROPERTIES
        IMPORTED_LOCATION_RELEASE "${_slideio_transformer_loc_release}"
        INTERFACE_INCLUDE_DIRECTORIES "${_slideio_include_genex}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
    )
    if(WIN32)
        set_target_properties(SlideIO::transformer PROPERTIES
            IMPORTED_IMPLIB_RELEASE "${SlideIO_TRANSFORMER_LIBRARY_RELEASE}")
    endif()
    if(SlideIO_TRANSFORMER_LIBRARY_DEBUG)
        set_target_properties(SlideIO::transformer PROPERTIES
            IMPORTED_LOCATION_DEBUG "${_slideio_transformer_loc_debug}"
        )
        if(WIN32)
            set_target_properties(SlideIO::transformer PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${SlideIO_TRANSFORMER_LIBRARY_DEBUG}")
        endif()
    else()
        set_target_properties(SlideIO::transformer PROPERTIES
            MAP_IMPORTED_CONFIG_DEBUG Release
        )
    endif()
```

Add the two new cache variables to the `mark_as_advanced(...)` list at the end of the file.

In `src/infra/CMakeLists.txt`, extend the existing link line:

```cmake
    PRIVATE SlideIO::slideio SlideIO::core SlideIO::transformer spdlog::spdlog
```

- [ ] **Step 3: Verify the build configures and links**

```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
cmake --build build/build --config Release
```

Expected: configures and builds clean. Nothing uses the transformer yet, so this only proves the find module is correct.

- [ ] **Step 4: Add the `SlideInfo` fields**

`ColorManagementAvailability` is already in `Types.h` from Task 2, so no new include is needed here — and none may be added, since `ColorManagement.h` includes `Types.h`. Add to `struct SlideInfo`, immediately after `colorProfileInfo`:

```cpp
    ColorManagementAvailability colorManagement = ColorManagementAvailability::NotColorimetric;
    std::string colorManagementDetail;  // SlideIO's message when colorManagement == BindFailed
    // Surfaced because isBrightfield is the wrong gate for ICC conversion in both
    // directions: it is false for 16-bit RGB brightfield, which ColorManagement
    // supports, and this hint is what separates 3x8-bit fluorescence from brightfield.
    bool fluorescenceHint = false;
```

- [ ] **Step 5: Populate `fluorescenceHint`**

In `src/infra/src/SlideIOAdapter.cpp`, both constructors compute a local `fluorescenceHint` for the `isBrightfield` heuristic. Immediately after each `bool fluorescenceHint = channelsIndicateFluorescence(*m_scene);`, add:

```cpp
    m_slideInfo.fluorescenceHint = fluorescenceHint;
```

- [ ] **Step 6: Build and run the suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: 3/3 pass, no behaviour change.

- [ ] **Step 7: Commit**

```bash
git add cmake/FindSlideIO.cmake src/infra/CMakeLists.txt \
        src/core/include/slideio/viewer/core/Types.h src/infra/src/SlideIOAdapter.cpp
git commit -m "Link slideio-transformer and carry colour management availability"
```

---

### Task 4: Dual scene in `SlideIOAdapter`

The feature itself. After this task the viewer can convert tiles; nothing in the UI can ask it to yet.

**Files:**
- Modify: `src/core/include/slideio/viewer/core/ISlideSource.h`
- Modify: `src/infra/include/slideio/viewer/infra/SlideIOAdapter.h`
- Modify: `src/infra/src/SlideIOAdapter.cpp`

**Interfaces:**
- Consumes: `ColorMode` (Task 1), `ColorManagementAvailability` (Task 2), `SlideInfo::fluorescenceHint` (Task 3).
- Produces: `ISlideSource::setColorMode(ColorMode)`, `ISlideSource::colorMode() const`, `ISlideSource::activeColorProfileInfo() const`; `SlideIOAdapter` constructors gaining a trailing `std::vector<uint8_t> defaultProfileBytes = {}` parameter.

- [ ] **Step 1: Write the capability tests**

These are integration tests — they open real files. Create `tests/infra/ColorManagedAdapterTest.cpp`; the CMake wiring that lets this binary find the SlideIO DLLs is Task 5, so this task's tests are written here and first run there. Write them now because they define the gate's behaviour.

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/core/ColorManagement.h"
#include "slideio/viewer/core/Types.h"

#include <cstdlib>
#include <string>

using namespace slideio::viewer;

namespace
{

// The image corpus is not part of the repository. Tests that need it are
// skipped rather than failed when it is absent, matching how SlideIO's own
// suite handles the same problem.
std::string imagePath(const std::string& relative)
{
    const char* root = std::getenv("SLIDEIO_VIEWER_TEST_IMAGES");
    return root ? std::string(root) + "/" + relative : std::string();
}

bool haveImage(const std::string& path)
{
    if (path.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

} // namespace

TEST_CASE("A slide with an embedded RGB profile offers colour management",
          "[infra][ColorManagement]")
{
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);
}

TEST_CASE("A slide with no profile and no default offers nothing", "[infra][ColorManagement]")
{
    const std::string path = imagePath("gdal/img_2448x2448_3x8bit_SRC_RGB_ducks.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::NoProfile);
}

TEST_CASE("A fluorescence slide is never colour-managed", "[infra][ColorManagement]")
{
    // Review Focus 1. A 3-channel 8-bit fluorescence image passes SlideIO's own
    // bind check, which counts channels and types but cannot know the channels
    // are intensities. Binding it would ICC-transform them as though they were
    // R/G/B. The viewer-side gate has to reject it before transformScene runs.
    const std::string path = imagePath("czi/08_18_2018_enc_1001_633.czi");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement
            == core::ColorManagementAvailability::NotColorimetric);
}

TEST_CASE("Colour management is off until it is asked for", "[infra][ColorManagement]")
{
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.colorMode() == core::ColorMode::Raw);
}
```

- [ ] **Step 2: Extend `ISlideSource`**

In `src/core/include/slideio/viewer/core/ISlideSource.h`, add to the class, following the existing defaulted-method idiom used by `addOnLevelMarkedUnreliable`:

```cpp
    // Colour mode selection. Backends that cannot convert colour ignore the
    // setter and always report Raw, so callers need not special-case them.
    virtual void setColorMode(ColorMode /*mode*/) {}
    virtual ColorMode colorMode() const { return ColorMode::Raw; }

    // The profile of the scene currently selected, which differs from
    // SlideInfo::colorProfileInfo: that one records what the file embeds, while
    // a colour-managed scene reports the profile it actually bound -- a supplied
    // default, for a slide that embeds none.
    virtual ColorProfileInfo activeColorProfileInfo() const { return {}; }
```

- [ ] **Step 3: Extend the adapter header**

In `src/infra/include/slideio/viewer/infra/SlideIOAdapter.h`, add `#include <atomic>` and `#include <cstdint>`, give both constructors a trailing parameter, and add the members and overrides:

```cpp
    // defaultProfileBytes stands in for slides that embed no profile; it is
    // ignored for slides that do. Empty means no default is configured.
    explicit SlideIOAdapter(const std::string& filePath, int sceneIndex = 0,
                            const std::string& driverId = "",
                            std::vector<uint8_t> defaultProfileBytes = {});
    SlideIOAdapter(const std::string& filePath, const std::string& auxImageName,
                   const std::string& driverId = "",
                   std::vector<uint8_t> defaultProfileBytes = {});

    void setColorMode(core::ColorMode mode) override;
    core::ColorMode colorMode() const override;
    core::ColorProfileInfo activeColorProfileInfo() const override;
```

```cpp
    // Built at open when the slide qualifies; null otherwise. Shares the origin's
    // underlying CVScene and its read serialisation mutex, so this is a second
    // Scene object, not a second reader or file handle.
    std::shared_ptr<::slideio::Scene> m_managedScene;
    std::atomic<core::ColorMode> m_colorMode{core::ColorMode::Raw};
```

- [ ] **Step 4: Add the wrapping helper**

In `src/infra/src/SlideIOAdapter.cpp`, add the includes:

```cpp
#include <slideio/transformer/transformer.hpp>
#include <slideio/transformer/colormanagementwrap.hpp>
```

In the anonymous namespace, after `convertColorProfileInfo`:

```cpp
// Build the colour-managed view of a scene, or report why there is not one.
//
// The gate is stricter than SlideIO's own. ColorManagement::bindToSource counts
// channels and checks data types, which a 3-channel 8-bit fluorescence image
// passes -- it would then ICC-transform three intensity channels as though they
// were R/G/B. Only the viewer knows the channels are fluorescence, so only the
// viewer can refuse.
slideio::viewer::core::ColorManagementAvailability buildManagedScene(
    const std::shared_ptr<::slideio::Scene>& scene,
    const slideio::viewer::core::SlideInfo& info,
    const std::vector<uint8_t>& defaultProfileBytes,
    std::shared_ptr<::slideio::Scene>& outManagedScene,
    std::string& outDetail)
{
    namespace core = slideio::viewer::core;
    outManagedScene.reset();
    outDetail.clear();

    if (!core::isColorimetricForIcc(info.numChannels, info.channelDataType,
                                    info.fluorescenceHint)) {
        return core::ColorManagementAvailability::NotColorimetric;
    }

    const bool haveDefault = !defaultProfileBytes.empty();
    if (!info.colorProfileInfo.present && !haveDefault) {
        return core::ColorManagementAvailability::NoProfile;
    }

    try {
        ::slideio::ColorManagementWrap cm;
        cm.setTarget(::slideio::ColorTarget::sRGB);
        // Fail, not AssumeSRGB: the gate above guarantees a profile, so this
        // path is unreachable. Fail turns a gate bug into a visible failure at
        // open rather than an identity transform claiming to be colour-managed.
        cm.setMissingProfilePolicy(::slideio::MissingProfilePolicy::Fail);
        // Applied only when the slide embeds nothing. SlideIO lets an override
        // displace an embedded profile, which is not what a default means.
        if (haveDefault && !info.colorProfileInfo.present) {
            cm.setSourceProfileOverride(::slideio::ColorProfile(defaultProfileBytes));
        }
        outManagedScene = ::slideio::transformScene(scene, cm);
        return core::ColorManagementAvailability::Available;
    } catch (const std::exception& ex) {
        outDetail = ex.what();
        spdlog::info("SlideIOAdapter: colour management unavailable: {}", ex.what());
        return core::ColorManagementAvailability::BindFailed;
    }
}
```

- [ ] **Step 5: Call it from both constructors**

In each constructor, after `m_slideInfo.colorProfileInfo` is populated and after `m_slideInfo.fluorescenceHint` is set, add:

```cpp
    m_slideInfo.colorManagement = buildManagedScene(m_scene, m_slideInfo, defaultProfileBytes,
                                                    m_managedScene, m_slideInfo.colorManagementDetail);
```

Ordering matters: the gate reads `colorProfileInfo`, `numChannels`, `channelDataType` and `fluorescenceHint`, so it must run after all four are assigned.

- [ ] **Step 6: Implement the accessors and the read selection**

Add to the `slideio::viewer::infra` namespace block:

```cpp
void SlideIOAdapter::setColorMode(core::ColorMode mode)
{
    m_colorMode.store(mode, std::memory_order_relaxed);
}

core::ColorMode SlideIOAdapter::colorMode() const
{
    return m_colorMode.load(std::memory_order_relaxed);
}

core::ColorProfileInfo SlideIOAdapter::activeColorProfileInfo() const
{
    const auto& scene = activeScene();
    try {
        return convertColorProfileInfo(scene->getColorProfileInfo());
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: activeColorProfileInfo failed: {}", ex.what());
        return {};
    }
}
```

Add a private helper to the header (`const std::shared_ptr<::slideio::Scene>& activeScene() const;`) and implement it:

```cpp
const std::shared_ptr<::slideio::Scene>& SlideIOAdapter::activeScene() const
{
    // Relaxed is sufficient: correctness comes from the mode being part of the
    // cache key, not from this load's ordering. A read that straddles a toggle
    // produces a valid tile of one mode or the other, and it is filed under
    // that mode's key either way.
    return (m_colorMode.load(std::memory_order_relaxed) == core::ColorMode::Managed
            && m_managedScene) ? m_managedScene : m_scene;
}
```

In `readTile`, replace both `m_scene->readResampled4DBlockChannels(...)` and `m_scene->readResampledBlockChannels(...)` with calls through a local taken once at the top of the read:

```cpp
    const auto& scene = activeScene();
```

Do the same in `readBlock`.

- [ ] **Step 7: Write the embedded-profile-wins test**

Review Focus 4. Append to `tests/infra/ColorManagedAdapterTest.cpp`:

```cpp
TEST_CASE("A default profile does not displace a profile the slide embeds",
          "[infra][ColorManagement]")
{
    // SlideIO's override wins unconditionally over an embedded profile
    // (colormanagement.cpp:48). A setting that means "use this when a slide has
    // none" must therefore not be passed through for a slide that has one --
    // otherwise configuring a default would silently change how every
    // profile-carrying slide renders.
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    std::vector<uint8_t> someOtherProfile(128, 0);  // never reaches SlideIO

    infra::SlideIOAdapter adapter(path, 0, "", someOtherProfile);
    adapter.setColorMode(core::ColorMode::Managed);

    const core::ColorProfileInfo active = adapter.activeColorProfileInfo();
    REQUIRE(active.present);
    REQUIRE(active.source == core::ColorProfileSource::Embedded);
    REQUIRE(active.description == "GIMP built-in sRGB");
}
```

- [ ] **Step 8: Build**

```bash
cmake --build build/build --config Release
```

Expected: clean. The new infra tests do not run yet — Task 5 wires them up.

- [ ] **Step 9: Commit**

```bash
git add src/core/include/slideio/viewer/core/ISlideSource.h \
        src/infra/include/slideio/viewer/infra/SlideIOAdapter.h \
        src/infra/src/SlideIOAdapter.cpp tests/infra/ColorManagedAdapterTest.cpp
git commit -m "Read tiles through a colour-managed scene when asked"
```

---

### Task 5: Run the infra integration tests

The only tests that prove the feature converts anything. Requires giving the infra test binary the SlideIO DLL path, which contradicts a comment at `tests/CMakeLists.txt:46` — approved in review as a deliberate departure.

**Files:**
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/infra/ColorManagedAdapterTest.cpp`

**Interfaces:**
- Consumes: everything from Task 4.
- Produces: `SlideIOAdapter::embeddedProfileBytes() const` (test support, added in Step 1).

- [ ] **Step 1: Add the pixel-difference tests**

Append to `tests/infra/ColorManagedAdapterTest.cpp`:

```cpp
TEST_CASE("Colour management changes the pixels of a profiled slide",
          "[infra][ColorManagement]")
{
    const std::string path = imagePath("gdal/colors.png");
    if (!haveImage(path)) { SKIP("test image corpus not available"); }

    infra::SlideIOAdapter adapter(path, 0, "");
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    const core::TileKey key(0, 0, 0);
    const core::TileData raw = adapter.readTile(key);
    adapter.setColorMode(core::ColorMode::Managed);
    const core::TileData managed = adapter.readTile(key);

    REQUIRE_FALSE(raw.isError());
    REQUIRE_FALSE(managed.isError());
    REQUIRE(raw.buffer().size() == managed.buffer().size());
    REQUIRE(raw.buffer() != managed.buffer());
}

TEST_CASE("A configured default profile colour-manages a slide that embeds none",
          "[infra][ColorManagement]")
{
    const std::string slide = imagePath("gdal/img_2448x2448_3x8bit_SRC_RGB_ducks.png");
    const std::string profileSource = imagePath("gdal/colors.png");
    if (!haveImage(slide) || !haveImage(profileSource)) { SKIP("test image corpus not available"); }

    // Borrow a real profile from a slide that has one, rather than ship an .icc
    // fixture: it keeps the corpus the single source of test data.
    std::vector<uint8_t> profileBytes;
    {
        infra::SlideIOAdapter donor(profileSource, 0, "");
        REQUIRE(donor.slideInfo().colorProfileInfo.present);
        profileBytes = donor.embeddedProfileBytes();
    }
    REQUIRE_FALSE(profileBytes.empty());

    infra::SlideIOAdapter adapter(slide, 0, "", profileBytes);
    REQUIRE(adapter.slideInfo().colorManagement == core::ColorManagementAvailability::Available);

    adapter.setColorMode(core::ColorMode::Managed);
    const core::ColorProfileInfo active = adapter.activeColorProfileInfo();
    REQUIRE(active.present);
    REQUIRE(active.source == core::ColorProfileSource::Supplied);
}
```

This needs one small accessor on the adapter. Add to `SlideIOAdapter.h`:

```cpp
    // Raw bytes of the profile the file embeds, empty when it embeds none.
    std::vector<uint8_t> embeddedProfileBytes() const;
```

and to `SlideIOAdapter.cpp`:

```cpp
std::vector<uint8_t> SlideIOAdapter::embeddedProfileBytes() const
{
    try {
        return m_scene->getColorProfile().getData();
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: embeddedProfileBytes failed: {}", ex.what());
        return {};
    }
}
```

- [ ] **Step 2: Register the test file and give the binary its runtime path**

In `tests/CMakeLists.txt`, add to the `slideio-viewer-infra-tests` source list:

```cmake
    infra/ColorManagedAdapterTest.cpp
```

Replace the comment at line 44-49 — it currently says the infra binary pulls in no SlideIO symbols, which stops being true here:

```cmake
# Both this binary and the UI one pull in SlideIO symbols, and nothing deploys
# runtime dependencies into the build tree. Copying the DLLs next to the
# executable is not enough: slideio.dll loads a family of sibling slideio-*.dll
# drivers, so point the tests at the install directories instead.
#
# SLIDEIO_VIEWER_TEST_IMAGES points at the image corpus, which is not part of
# this repository; tests needing it skip when it is unset or the file is absent.
```

After the `ui-tests` `add_test`, extend the Windows block to cover both targets:

```cmake
if(WIN32)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set(_slideio_bin_dir "$<IF:$<CONFIG:Debug>,${SLIDEIO_ROOT}/debug/bin,${SLIDEIO_ROOT}/release/bin>")
        set_tests_properties(ui-tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_slideio_bin_dir};PATH=path_list_prepend:${_qt_bin_dir}")
        set_tests_properties(infra-tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_slideio_bin_dir}")
    else()
        message(WARNING
            "CMake < 3.22: the ui-tests and infra-tests targets cannot have their PATH set up "
            "automatically. Run them with the Qt and SlideIO bin directories on PATH.")
    endif()
endif()
```

- [ ] **Step 3: Run the tests with the corpus available**

```bash
export PATH="/c/Users/Stanislav/anaconda3/envs/conan2/Scripts:$PATH"
export SLIDEIO_VIEWER_TEST_IMAGES="D:/Projects/slideio/images/images"
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: 3/3 test binaries pass, with the colour-management cases running rather than skipping. If they skip, `SLIDEIO_VIEWER_TEST_IMAGES` is not reaching the test — `ENVIRONMENT_MODIFICATION` does not forward it, so confirm it is set in the shell running `ctest`.

- [ ] **Step 4: Confirm they skip cleanly without the corpus**

```bash
unset SLIDEIO_VIEWER_TEST_IMAGES
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: 3/3 still pass, colour-management cases reported as skipped. A developer without the corpus must not see a red suite.

- [ ] **Step 5: Commit**

```bash
git add tests/CMakeLists.txt tests/infra/ColorManagedAdapterTest.cpp \
        src/infra/include/slideio/viewer/infra/SlideIOAdapter.h src/infra/src/SlideIOAdapter.cpp
git commit -m "Prove colour management converts tiles, against the image corpus"
```

---

### Task 6: Stamp the active colour mode onto tile keys

Without this the UI would request and cache managed tiles under raw keys.

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportController.h`
- Modify: `src/ui/src/ViewportController.cpp:94-107`
- Modify: `src/ui/src/ViewportWidget.cpp:1786`

**Interfaces:**
- Consumes: `core::ColorMode` (Task 1).
- Produces: `ViewportController::setColorMode(core::ColorMode)` and `colorMode() const`.

- [ ] **Step 1: Extend `ViewportController`**

Add to the header:

```cpp
    void setColorMode(core::ColorMode mode);
    core::ColorMode colorMode() const;
```

and the member `core::ColorMode m_colorMode = core::ColorMode::Raw;`.

- [ ] **Step 2: Fold the mode into the existing key remap**

`visibleTileKeys()` already re-stamps the keys `TilePyramid` produces so they carry the current Z/T. Extend that remap rather than teaching `TilePyramid` about colour, which would pull a rendering concern into the geometry layer. Replace the conditional remap at `ViewportController.cpp:100-106` with an unconditional one:

```cpp
    auto keys = m_coordSystem->visibleTiles(m_viewport);
    // TilePyramid yields geometry only -- level, column, row. The remaining
    // dimensions of a tile's identity are view state, so they are stamped here:
    // Z/T as before, and now the colour mode, so raw and managed tiles of the
    // same region stay distinct in the cache.
    for (auto& key : keys) {
        key = core::TileKey(key.level(), key.column(), key.row(), m_zIndex, m_tFrame, m_colorMode);
    }
    return keys;
```

Implement the two accessors:

```cpp
void ViewportController::setColorMode(core::ColorMode mode)
{
    m_colorMode = mode;
}

core::ColorMode ViewportController::colorMode() const
{
    return m_colorMode;
}
```

- [ ] **Step 3: Stamp the fallback key**

In `ViewportWidget.cpp`, `findFallbackTile` builds a key to look up in `textures`. A raw-keyed lookup would never hit a managed texture, so the coarse-tile fallback would silently stop working in managed mode. Replace line 1786:

```cpp
            core::TileKey fallbackKey(fallbackLevel, fallbackCol, fallbackRow,
                                      key.zIndex(), key.tFrame(), key.colorMode());
```

Note this also fixes a latent bug: the fallback key previously dropped the Z and T indices of the key it was a fallback for.

- [ ] **Step 4: Build and run the suite**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: 3/3 pass. Behaviour is unchanged because nothing sets the mode to `Managed` yet.

- [ ] **Step 5: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportController.h \
        src/ui/src/ViewportController.cpp src/ui/src/ViewportWidget.cpp
git commit -m "Stamp the colour mode onto requested tile keys"
```

---

### Task 7: The default ICC profile setting

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `src/ui/src/ViewportWidget.cpp` (`openSceneSync`, `openAuxImageSync`, `Impl`)
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`

**Interfaces:**
- Consumes: `inspectIccHeader`, `IccHeaderSummary` (Task 2); the adapter's new constructor parameter (Task 4).
- Produces: `ViewportWidget::setDefaultColorProfile(std::vector<uint8_t>)`; `QSettings` key `color/defaultSourceProfile`.

- [ ] **Step 1: Carry the bytes into the open path**

`SlideIOAdapter` is constructed by `openSceneSync` and `openAuxImageSync` in `ViewportWidget.cpp`, on a background thread — not by `SlideViewerService`. `QSettings` is read on the UI thread and the bytes handed down, so no settings access happens off-thread.

Add to `ViewportWidget.h`:

```cpp
    // Bytes of the profile to assume for slides embedding none. Set before a
    // slide is opened; empty means no default is configured.
    void setDefaultColorProfile(std::vector<uint8_t> bytes);
```

Add `std::vector<uint8_t> defaultColorProfile;` to `ViewportWidget::Impl`, the setter, and a trailing `std::vector<uint8_t> defaultProfileBytes` parameter on both `openSceneSync` and `openAuxImageSync`, forwarded to the adapter constructor. Capture `m_impl->defaultColorProfile` by value at each async call site.

- [ ] **Step 2: Add the menu items**

In `MainWindow.cpp`, create the actions alongside the existing ones and add them to the View menu after `metadataToggleAction`:

```cpp
        viewMenu->addSeparator();
        viewMenu->addAction(setDefaultProfileAction);
        viewMenu->addAction(clearDefaultProfileAction);
```

```cpp
        setDefaultProfileAction = new QAction("Set Default ICC Profile…", owner);
        clearDefaultProfileAction = new QAction("Clear Default ICC Profile", owner);
```

- [ ] **Step 3: Implement choosing a profile**

```cpp
void MainWindow::onSetDefaultColorProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Default ICC Profile"), QString(),
        tr("ICC profiles (*.icc *.icm);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Default ICC Profile"),
                             tr("Could not read %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    const QByteArray raw = file.readAll();
    const std::vector<uint8_t> bytes(raw.begin(), raw.end());

    // Checked here rather than left to fail at the next slide open: SlideIO
    // treats an unusable profile as absence, so without this the setting would
    // appear to take and then quietly do nothing.
    const core::IccHeaderSummary summary = core::inspectIccHeader(bytes);
    if (!summary.plausible) {
        QMessageBox::warning(this, tr("Default ICC Profile"),
                             tr("%1 is not a valid ICC profile.")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (summary.dataSpace != "RGB ") {
        QMessageBox::warning(
            this, tr("Default ICC Profile"),
            tr("%1 describes %2 data. Color management needs an RGB profile.")
                .arg(QDir::toNativeSeparators(path),
                     QString::fromStdString(summary.dataSpace).trimmed()));
        return;
    }

    QSettings settings;
    settings.setValue(kDefaultProfileKey, path);
    applyDefaultColorProfile();
}
```

Add the key as a file-scope constant: `constexpr auto kDefaultProfileKey = "color/defaultSourceProfile";`

- [ ] **Step 4: Implement clearing and loading**

```cpp
void MainWindow::onClearDefaultColorProfile()
{
    QSettings settings;
    settings.remove(kDefaultProfileKey);
    applyDefaultColorProfile();
}

// Reads the configured profile from disk and hands it to the viewport. The path
// is stored, not the bytes, so replacing the file on disk takes effect on the
// next slide open rather than needing the setting to be re-chosen.
void MainWindow::applyDefaultColorProfile()
{
    QSettings settings;
    const QString path = settings.value(kDefaultProfileKey).toString();
    std::vector<uint8_t> bytes;
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray raw = file.readAll();
            bytes.assign(raw.begin(), raw.end());
        } else {
            spdlog::warn("MainWindow: default ICC profile '{}' could not be read; "
                         "treating it as unconfigured",
                         path.toStdString());
        }
    }
    m_impl->clearDefaultProfileAction->setEnabled(!path.isEmpty());
    m_impl->viewportWidget->setDefaultColorProfile(std::move(bytes));
}
```

Call `applyDefaultColorProfile()` once during construction, after the viewport widget exists.

- [ ] **Step 5: Connect the actions**

```cpp
        connect(setDefaultProfileAction, &QAction::triggered,
                owner, &MainWindow::onSetDefaultColorProfile);
        connect(clearDefaultProfileAction, &QAction::triggered,
                owner, &MainWindow::onClearDefaultColorProfile);
```

- [ ] **Step 6: Build and verify manually**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
./build/install/release/bin/slideio-viewer.exe "D:/Projects/slideio/images/images/gdal/colors.png"
```

Check: `Set Default ICC Profile…` rejects a non-ICC file (point it at any `.png`) with a clear message; `Clear` is disabled until one is set.

- [ ] **Step 7: Verify the unreadable-path case**

Review Focus 5. Set a default profile, then rename the file on disk and open a profile-less slide. Expected: a warning in `%LOCALAPPDATA%\SlideIO\SlideIO Viewer\logs\slideio-viewer.log` reading "could not be read; treating it as unconfigured", the slide opens normally, and the properties panel reports no profile. No crash, and nothing claiming to be colour-managed.

- [ ] **Step 8: Commit**

```bash
git add src/ui/src/MainWindow.cpp src/ui/src/ViewportWidget.cpp \
        src/ui/include/slideio/viewer/ui/ViewportWidget.h
git commit -m "Let a default ICC profile stand in for slides that embed none"
```

---

### Task 8: The colour management toggle

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `src/ui/include/slideio/viewer/ui/StatusBarManager.h`
- Modify: `src/ui/src/StatusBarManager.cpp`
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/ViewportWidget.cpp`
- Modify: `src/ui/include/slideio/viewer/ui/SlidePropertiesPanel.h`
- Modify: `src/ui/src/SlidePropertiesPanel.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: `QSettings` key `view/colorManagement`; `ViewportWidget::setColorMode(core::ColorMode)`; `ViewportWidget::activeColorProfileInfo() const`; `StatusBarManager::setColorManaged(bool)`; `SlidePropertiesPanel::setActiveColorProfile(const core::ColorProfileInfo&)`.

- [ ] **Step 1: Route the mode through the viewport**

In `ViewportWidget`:

```cpp
void ViewportWidget::setColorMode(core::ColorMode mode)
{
    if (!m_impl->slideSource) {
        return;
    }
    m_impl->slideSource->setColorMode(mode);
    m_impl->controller->setColorMode(mode);
    releaseTexturesOfOtherMode(mode);
    // No cache clear and no scheduler cancellation: the mode is part of the key,
    // so tiles of the other mode are simply not looked up, and any read already
    // in flight files its result under the key it was issued with.
    m_impl->controller->requestVisibleTiles();
    update();
}
```

`m_impl->textures` and `m_impl->fluorescenceTextures` are keyed by `TileKey`, so without this they would accumulate both modes' GPU textures and double video memory on the first toggle. The CPU-side `LruTileCache` is the right place to keep both renditions — it has a budget and an eviction policy — but video memory is scarcer, so the inactive mode is dropped from the GPU and re-uploaded from the still-cached CPU tiles, which costs no decode and no I/O:

```cpp
void ViewportWidget::releaseTexturesOfOtherMode(core::ColorMode keep)
{
    if (!m_impl->gl) {
        return;
    }
    makeCurrent();
    for (auto it = m_impl->textures.begin(); it != m_impl->textures.end(); ) {
        if (it->first.colorMode() != keep) {
            m_impl->gl->glDeleteTextures(1, &it->second);
            it = m_impl->textures.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_impl->fluorescenceTextures.begin();
         it != m_impl->fluorescenceTextures.end(); ) {
        if (it->first.colorMode() != keep) {
            for (GLuint id : it->second.channelTexIds) {
                m_impl->gl->glDeleteTextures(1, &id);
            }
            it = m_impl->fluorescenceTextures.erase(it);
        } else {
            ++it;
        }
    }
    doneCurrent();
}
```

- [ ] **Step 2: Add the menu action**

```cpp
        colorManagementAction = new QAction("Color Management", owner);
        colorManagementAction->setCheckable(true);
        colorManagementAction->setShortcut(QKeySequence("Ctrl+Shift+C"));
```

Added to the View menu before the default-profile separator from Task 7.

- [ ] **Step 3: Set the action's state when a slide opens**

In the existing slide-opened handler, beside the `propertiesPanel->setSlideInfo(info)` call at `MainWindow.cpp:345`:

```cpp
                const bool available =
                    info.colorManagement == core::ColorManagementAvailability::Available;
                m_impl->colorManagementAction->setEnabled(available);
                m_impl->colorManagementAction->setToolTip(QString::fromStdString(
                    core::colorManagementUnavailableReason(info.colorManagement,
                                                           info.colorManagementDetail)));

                QSettings settings;
                const bool wanted = available
                    && settings.value(kColorManagementKey, false).toBool();
                m_impl->colorManagementAction->setChecked(wanted);
                m_impl->viewportWidget->setColorMode(
                    wanted ? core::ColorMode::Managed : core::ColorMode::Raw);
                m_impl->statusBarManager->setColorManaged(wanted);
```

Add `constexpr auto kColorManagementKey = "view/colorManagement";`.

The remembered preference is applied only where it is available, so a slide that cannot be managed never shows a checked box.

- [ ] **Step 4: Handle the toggle**

```cpp
void MainWindow::onColorManagementToggled(bool enabled)
{
    QSettings settings;
    settings.setValue(kColorManagementKey, enabled);

    m_impl->viewportWidget->setColorMode(enabled ? core::ColorMode::Managed
                                                 : core::ColorMode::Raw);
    m_impl->statusBarManager->setColorManaged(enabled);
    // The two scenes report different provenance -- the wrapper reports the
    // profile it bound, which for a supplied default is not what the file
    // carries -- so the panel is refreshed from the active scene, not from the
    // SlideInfo captured at open.
    m_impl->propertiesPanel->setActiveColorProfile(
        m_impl->viewportWidget->activeColorProfileInfo());
}
```

Add a matching `activeColorProfileInfo()` pass-through on `ViewportWidget` that forwards to `m_impl->slideSource`, returning `{}` when no slide is open.

- [ ] **Step 5: Status bar indicator**

Add to `StatusBarManager`:

```cpp
    void setColorManaged(bool managed);
```

```cpp
    QLabel* m_colorLabel;
```

```cpp
void StatusBarManager::setColorManaged(bool managed)
{
    m_colorLabel->setText(managed ? QStringLiteral("sRGB") : QString());
    m_colorLabel->setVisible(managed);
}
```

Create the label in `setup()` next to the magnification label, hidden initially.

- [ ] **Step 6: Panel refresh**

Add to `SlidePropertiesPanel`:

```cpp
    // Replace the colour-profile rows with the profile of the scene currently
    // being read, leaving every other row as setSlideInfo left it.
    void setActiveColorProfile(const core::ColorProfileInfo& info);
```

Refactor the colour-profile block added in `54ac47a` out of `setSlideInfo` into a private `rebuildColorProfileRows(const core::ColorProfileInfo&)`, called by both. It removes any existing top-level item titled `Color profile` before rebuilding, so repeated toggles do not accumulate rows.

- [ ] **Step 7: Connect and build**

```cpp
        connect(colorManagementAction, &QAction::toggled,
                owner, &MainWindow::onColorManagementToggled);
```

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release --output-on-failure
cmake --install build/build --config Release
```

- [ ] **Step 8: Verify the four slide classes by hand**

Launch each and check the View menu and properties panel:

| Slide | Expected |
|---|---|
| `gdal/colors.png` | Enabled; toggling visibly changes the colour bars; `Source` stays `Embedded` |
| `gdal/img_2448x2448_3x8bit_SRC_RGB_ducks.png`, no default set | Greyed, tooltip "embeds no ICC profile, and no default profile is set" |
| the same slide, default set | Enabled; toggling changes the image; `Source` reads `Supplied` |
| `czi/08_18_2018_enc_1001_633.czi` | Greyed, tooltip "applies to RGB brightfield slides only" |

Then, on `gdal/colors.png`, toggle rapidly a dozen times while panning. Review Focus 3: no tile may ever render in the wrong mode, and no stale tile may appear after the view settles. Watch GPU memory across those toggles too — it must return to roughly its pre-toggle level each time rather than climbing, which is what `releaseTexturesOfOtherMode` is there to ensure.

- [ ] **Step 9: Commit**

```bash
git add src/ui/
git commit -m "Add the colour management toggle"
```

---

### Task 9: Measure, then record what was measured

**Files:**
- Modify: `docs/superpowers/specs/2026-10-06-color-management-design.md` (§10)

- [ ] **Step 1: Measure slide open**

Time `SlideIOAdapter` construction on a large SVS (`svs/JP2K-33003-1.svs`) with eager wrapping, and again with the `buildManagedScene` call commented out. Use the existing `spdlog` timing idiom, Release build, three runs each, warm cache.

- [ ] **Step 2: Decide on eagerness**

If eager wrapping adds more than ~5% to adapter construction, change `buildManagedScene` to defer `transformScene` to the first `setColorMode(Managed)`, keeping the gate as the predictor of availability. Otherwise leave it eager — the spec's fallback, not a new decision.

- [ ] **Step 3: Measure tile reads**

Time `readTile` in both modes on the same slide, 100 tiles each, and record the per-tile delta.

- [ ] **Step 4: Record the numbers and mark the spec implemented**

Replace §10's "Expected to be..." prose with the measured figures and change the spec's `**Status:**` line to `Implemented`.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-10-06-color-management-design.md
git commit -m "Record the measured cost of colour management"
```
