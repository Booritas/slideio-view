# Annotation Persistence (E1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Annotations survive closing a slide, changing a colour profile, and quitting — written as JSON into a user-visible workspace directory and loaded back when the same slide and scene reopen.

**Architecture:** Serialization is pure C++ in `core` (so every parse failure is testable in the one suite that runs without Qt DLL plumbing). `infra` implements the repository over `QSaveFile`. An `app`-layer `AnnotationPersistenceService` owns the save/load policy and the dirty flag; `ViewportWidget` calls it at exactly two lifecycle points, with the flush strictly before the existing `resetAnnotationState()`.

**Tech Stack:** C++17, Qt 6 Widgets, nlohmann_json 3.11.3 (already a Conan requirement, never linked until now), Catch2 v3, CMake 3.20+/Conan 2.

**Spec:** `docs/superpowers/specs/2026-10-10-annotation-persistence-design.md`

## Global Constraints

- **C++17.** No `std::format`, no `std::chrono::parse`, no `std::filesystem` in `core` headers.
- **`core` is Qt-free.** It links no Qt target. `nlohmann_json` is linked `PRIVATE` so it never appears in a `core` public header.
- **Layer rule:** `core` → `app` → `infra` → `ui`, downward only. `infra` implements interfaces declared in `core`.
- **Naming:** `PascalCase` types, `camelCase` functions, `m_camelCase` members, `kPascalCase` constants, `lowercase` namespaces, `PascalCase.h/.cpp` files.
- **Style:** 4-space indent, 120-char lines, Allman braces for classes and functions, K&R for control flow, `#pragma once`.
- **Include order:** own header, project headers, Qt headers, std headers.
- **AUTOMOC does not scan a header in a different directory from its `.cpp`.** Any new `Q_OBJECT` header under `include/` must be listed explicitly in its target's sources — `src/app/CMakeLists.txt` already does this for `AnnotationModel.h`.
- **Never run `app-tests`, `infra-tests` or `ui-tests` as a bare `.exe`.** They find their DLLs through a ctest `ENVIRONMENT_MODIFICATION` property; running the executable directly pops a modal Windows error box that blocks until a human dismisses it. Only `core-tests` is safe to run directly.
- **No test suite creates a `QApplication` or `QCoreApplication`.** No test may construct a widget, and no `QTimer` will ever fire in a test.
- **Build:** `cmake --build build/build --config Release`
- **Test:** `ctest --test-dir build/build -C Release --output-on-failure`
- **Schema version is exactly `1`.** File name: `<slideId>.s<sceneIndex>.annotations.json`.
- **Colours serialize as `#RRGGBBAA`.** Timestamps as ISO 8601 UTC with a `Z` suffix, whole seconds.
- **Autosave: 2000 ms debounce, 30000 ms backstop.**
- **Existing defaults that must round-trip unchanged:** `Color{0xE6, 0x7E, 0x22, 0xFF}`, `lineWidth = 2.0f` (screen pixels), `fillOpacity = 0.3f`.

## Review Focus

Failure modes the spec implies that no task's happy path would exercise. Each has a test pinned to the task that owns the code.

1. **A slide whose scenes cannot be enumerated** yields an empty `slideId`; annotation tools must be disabled and nothing written, or an hour of drawing is discarded silently. → Task 9.
2. **An associated image** (label/macro) computes the same file-level slide ID with `sceneIndex = 0`; annotating one must be impossible, or its rectangles land in scene 0's file and get painted over the tissue. → Task 9.
3. **A workspace directory that exists but is not writable** must report `NotWritable` naming the path, never fall back to the default folder. → Task 5.
4. **An annotation file whose embedded `slideId` differs from the computed one** must reach the mismatch resolver, never load silently and never be overwritten. → Task 7.
5. **Non-finite coordinates reaching the serializer** must be refused, not written as JSON `null` — which produces a file that cannot be parsed back. → Task 2.

---

### Task 1: ISO 8601 timestamp helpers in core

**Files:**
- Create: `src/core/include/slideio/viewer/core/Iso8601.h`
- Create: `src/core/src/Iso8601.cpp`
- Test: `tests/core/Iso8601Test.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `std::string formatIso8601Utc(std::chrono::system_clock::time_point)` and `bool parseIso8601Utc(std::string_view, std::chrono::system_clock::time_point&)` in `slideio::viewer::core`.

**Why this is its own task:** C++17 has no `std::chrono::parse`, the platform spellings differ (`timegm` vs `_mkgmtime`, `gmtime_r` vs `gmtime_s`), and the representable range has a hard floor that the rest of the plan depends on knowing about.

- [ ] **Step 1: Write the failing test**

Create `tests/core/Iso8601Test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/Iso8601.h"

#include <chrono>
#include <string>

using namespace slideio::viewer::core;

namespace
{

/// The epoch plus a fixed offset, so the expected string is arithmetic rather
/// than a second implementation of the formatter.
std::chrono::system_clock::time_point epochPlus(long long seconds)
{
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(seconds));
}

} // namespace

TEST_CASE("the epoch formats as the ISO 8601 epoch", "[core][Iso8601]")
{
    REQUIRE(formatIso8601Utc(epochPlus(0)) == "1970-01-01T00:00:00Z");
}

TEST_CASE("a known instant formats with zero padding everywhere", "[core][Iso8601]")
{
    // 2026-03-05T04:07:09Z -- single-digit month, day, hour, minute and second,
    // which is where a format string without padding goes wrong.
    REQUIRE(formatIso8601Utc(epochPlus(1772683629LL)) == "2026-03-05T04:07:09Z");
}

TEST_CASE("formatting round-trips through parsing", "[core][Iso8601]")
{
    const auto original = epochPlus(1772683629LL);
    std::chrono::system_clock::time_point parsed{};
    REQUIRE(parseIso8601Utc(formatIso8601Utc(original), parsed));
    REQUIRE(parsed == original);
}

TEST_CASE("sub-second precision is truncated, not rounded", "[core][Iso8601]")
{
    const auto t = epochPlus(1772683629LL) + std::chrono::milliseconds(999);
    REQUIRE(formatIso8601Utc(t) == "2026-03-05T04:07:09Z");
}

TEST_CASE("parsing rejects everything that is not the exact format", "[core][Iso8601]")
{
    std::chrono::system_clock::time_point out{};
    REQUIRE_FALSE(parseIso8601Utc("", out));
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05T04:07:09", out));        // no Z
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05 04:07:09Z", out));       // space, not T
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05T04:07:09+01:00", out));  // offset
    REQUIRE_FALSE(parseIso8601Utc("2026-3-5T4:7:9Z", out));            // unpadded
    REQUIRE_FALSE(parseIso8601Utc("2026-13-05T04:07:09Z", out));       // month 13
    REQUIRE_FALSE(parseIso8601Utc("2026-03-32T04:07:09Z", out));       // day 32
    REQUIRE_FALSE(parseIso8601Utc("2026-03-05T24:07:09Z", out));       // hour 24
    REQUIRE_FALSE(parseIso8601Utc("not-a-timestamp-abcZ", out));       // right length
}

TEST_CASE("a failed parse leaves the output untouched", "[core][Iso8601]")
{
    const auto sentinel = epochPlus(1234);
    auto out = sentinel;
    REQUIRE_FALSE(parseIso8601Utc("garbage", out));
    REQUIRE(out == sentinel);
}

TEST_CASE("instants before 1970 are out of range and are rejected", "[core][Iso8601]")
{
    // _mkgmtime on MSVC cannot represent them, so the format is documented as
    // starting at the epoch and the parser says no rather than inventing a value.
    std::chrono::system_clock::time_point out{};
    REQUIRE_FALSE(parseIso8601Utc("1969-12-31T23:59:59Z", out));
}
```

Add `core/Iso8601Test.cpp` to the `slideio-viewer-core-tests` source list in `tests/CMakeLists.txt`, after `core/AnnotationTest.cpp`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `slideio/viewer/core/Iso8601.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/core/include/slideio/viewer/core/Iso8601.h`:

```cpp
#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace slideio::viewer::core
{

/// Formats as "2026-10-10T14:30:00Z". Sub-second precision is truncated.
///
/// The representable range starts at 1970-01-01T00:00:00Z. Annotation
/// timestamps are always "now" or a value read back from a file this
/// application wrote, so the floor costs nothing; `parseIso8601Utc` rejects
/// anything below it rather than inventing a value.
std::string formatIso8601Utc(std::chrono::system_clock::time_point t);

/// Accepts exactly what formatIso8601Utc produces: 20 characters, zero-padded,
/// 'T' separator, 'Z' suffix, no offsets and no fractional seconds. Returns
/// false and leaves `out` untouched on anything else, including instants before
/// the epoch.
bool parseIso8601Utc(std::string_view text, std::chrono::system_clock::time_point& out);

} // namespace slideio::viewer::core
```

- [ ] **Step 4: Write the implementation**

Create `src/core/src/Iso8601.cpp`:

```cpp
#include "slideio/viewer/core/Iso8601.h"

#include <array>
#include <cstdio>
#include <ctime>

namespace slideio::viewer::core
{

namespace
{

std::tm toUtcTm(std::time_t seconds)
{
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    return tm;
}

/// -1 doubles as "out of range" here. It is also a legitimate instant
/// (1969-12-31T23:59:59Z), but the format's floor is the epoch, so treating it
/// as failure is correct rather than merely convenient.
std::time_t fromUtcTm(std::tm& tm)
{
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

} // namespace

std::string formatIso8601Utc(std::chrono::system_clock::time_point t)
{
    const auto truncated = std::chrono::time_point_cast<std::chrono::seconds>(t);
    const std::time_t seconds = std::chrono::system_clock::to_time_t(truncated);
    const std::tm tm = toUtcTm(seconds);

    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(),
                                      "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                      tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (written <= 0) {
        return {};
    }
    return std::string(buffer.data(), static_cast<std::size_t>(written));
}

bool parseIso8601Utc(std::string_view text, std::chrono::system_clock::time_point& out)
{
    if (text.size() != 20) {
        return false;
    }

    // sscanf needs a NUL-terminated buffer, and string_view does not promise one.
    const std::string copy(text);
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    char suffix = '\0';
    const int fields = std::sscanf(copy.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c",
                                   &year, &month, &day, &hour, &minute, &second, &suffix);
    if (fields != 7 || suffix != 'Z') {
        return false;
    }

    // sscanf's %2d happily accepts "3-" where the format demands "03", so the
    // separators are checked by position rather than trusted to the scan.
    if (copy[4] != '-' || copy[7] != '-' || copy[10] != 'T' || copy[13] != ':' || copy[16] != ':') {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return false;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;

    const std::time_t seconds = fromUtcTm(tm);
    if (seconds == static_cast<std::time_t>(-1)) {
        return false;
    }
    out = std::chrono::system_clock::from_time_t(seconds);
    return true;
}

} // namespace slideio::viewer::core
```

Add `src/Iso8601.cpp` to the `slideio-viewer-core` source list in `src/core/CMakeLists.txt`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS, all `[core][Iso8601]` cases green.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/Iso8601.h src/core/src/Iso8601.cpp \
        src/core/CMakeLists.txt tests/core/Iso8601Test.cpp tests/CMakeLists.txt
git commit -m "Add ISO 8601 UTC timestamp helpers to core"
```

---

### Task 2: The annotation document and its serializer

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationDocument.h`
- Create: `src/core/include/slideio/viewer/core/AnnotationSerialization.h`
- Create: `src/core/src/AnnotationSerialization.cpp`
- Modify: `src/core/include/slideio/viewer/core/Annotation.h`, `src/core/src/Annotation.cpp`
- Modify: `src/core/CMakeLists.txt` (sources + `nlohmann_json` PRIVATE)
- Modify: `CMakeLists.txt` (top level — `find_package(nlohmann_json REQUIRED)`)
- Test: `tests/core/AnnotationSerializationTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `formatIso8601Utc` from Task 1; the existing `core::Annotation`, `core::AnnotationGeometry`, `core::Color`, `core::PointF`.
- Produces: `struct SlideProvenance`, `struct AnnotationDocument`, `inline constexpr int kCurrentSchemaVersion = 1`, `std::string serializeAnnotationDocument(const AnnotationDocument&)`, and a five-argument `Annotation` constructor taking properties and metadata.

**Note on the `Annotation` constructor:** the existing three-argument constructor stamps `createdAt`/`modifiedAt` with the current time. Loading must restore the stored values instead, so a second constructor is required. This is also what lets the model stamp an author at creation (Task 6).

- [ ] **Step 1: Write the failing test**

Create `tests/core/AnnotationSerializationTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AnnotationSerialization.h"
#include "slideio/viewer/core/Iso8601.h"

#include <chrono>
#include <limits>
#include <string>

using namespace slideio::viewer::core;

namespace
{

std::chrono::system_clock::time_point at(long long epochSeconds)
{
    return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(epochSeconds));
}

Annotation makeAnnotation()
{
    AnnotationProperties properties;
    properties.label = "Tumour focus";
    properties.classification = "Carcinoma";
    properties.color = Color{0x1F, 0x77, 0xB4, 0x80};
    properties.lineWidth = 3.5f;
    properties.fillOpacity = 0.25f;
    properties.notes = "Checked under 40x";

    AnnotationMetadata metadata;
    metadata.author = "s.melnikov";
    metadata.createdAt = at(1772683629LL);
    metadata.modifiedAt = at(1772683700LL);

    return Annotation("4f1c8e2a-91b7-4d3e-8c15-2a9f7e6b0d44", AnnotationType::Rectangle,
                      RectangleGeometry{PointF{1200.0, 800.0}, PointF{4400.0, 2600.0}},
                      properties, metadata);
}

AnnotationDocument makeDocument()
{
    AnnotationDocument document;
    document.slideId = "a3f8c2e1b4d50697";
    document.sceneIndex = 0;
    document.slide.fileName = "case001_HE.svs";
    document.slide.path = "D:/data/pathology/case001_HE.svs";
    document.slide.width = 102400;
    document.slide.height = 76800;
    document.createdAt = at(1772683600LL);
    document.modifiedAt = at(1772683700LL);
    document.annotations.push_back(makeAnnotation());
    return document;
}

} // namespace

TEST_CASE("an empty document serializes with the current schema version", "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "a3f8c2e1b4d50697";
    const std::string json = serializeAnnotationDocument(document);

    REQUIRE_FALSE(json.empty());
    REQUIRE(json.find("\"schemaVersion\": 1") != std::string::npos);
    REQUIRE(json.find("\"annotations\": []") != std::string::npos);
}

TEST_CASE("the document header is written verbatim", "[core][AnnotationSerialization]")
{
    const std::string json = serializeAnnotationDocument(makeDocument());

    REQUIRE(json.find("\"slideId\": \"a3f8c2e1b4d50697\"") != std::string::npos);
    REQUIRE(json.find("\"sceneIndex\": 0") != std::string::npos);
    REQUIRE(json.find("\"fileName\": \"case001_HE.svs\"") != std::string::npos);
    REQUIRE(json.find("\"width\": 102400") != std::string::npos);
    REQUIRE(json.find("\"createdAt\": \"2026-03-05T04:06:40Z\"") != std::string::npos);
}

TEST_CASE("a rectangle writes its own corners, not a normalised box", "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    // Deliberately inverted: the serializer records what was drawn. Normalising
    // here would make a round trip lossy for a drag that went up and left.
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{400.0, 300.0}, PointF{100.0, 50.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("\"topLeft\"") != std::string::npos);
    REQUIRE(json.find("400.0") != std::string::npos);
    REQUIRE(json.find("50.0") != std::string::npos);
}

TEST_CASE("a colour is written as #RRGGBBAA in upper case", "[core][AnnotationSerialization]")
{
    const std::string json = serializeAnnotationDocument(makeDocument());
    REQUIRE(json.find("\"color\": \"#1F77B480\"") != std::string::npos);
}

TEST_CASE("the default annotation colour survives verbatim", "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("\"color\": \"#E67E22FF\"") != std::string::npos);
}

TEST_CASE("a non-finite coordinate refuses to serialize", "[core][AnnotationSerialization]")
{
    // JSON cannot carry NaN or infinity. nlohmann emits null, which produces a
    // file that will not parse back -- so the write is refused instead.
    AnnotationDocument document;
    document.slideId = "id";

    SECTION("NaN")
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                          RectangleGeometry{PointF{0.0, 0.0}, PointF{nan, 1.0}});
        REQUIRE(serializeAnnotationDocument(document).empty());
    }

    SECTION("infinity")
    {
        const double inf = std::numeric_limits<double>::infinity();
        document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                          RectangleGeometry{PointF{0.0, inf}, PointF{1.0, 1.0}});
        REQUIRE(serializeAnnotationDocument(document).empty());
    }
}

TEST_CASE("a double needing full precision is written at full precision",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    const double awkward = 0.1 + 0.2;   // 0.30000000000000004
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{awkward, 0.0}, PointF{1.0, 1.0}});

    const std::string json = serializeAnnotationDocument(document);
    REQUIRE(json.find("0.30000000000000004") != std::string::npos);
}

TEST_CASE("annotation metadata is written, not regenerated", "[core][AnnotationSerialization]")
{
    const std::string json = serializeAnnotationDocument(makeDocument());
    REQUIRE(json.find("\"author\": \"s.melnikov\"") != std::string::npos);
    REQUIRE(json.find("\"createdAt\": \"2026-03-05T04:07:09Z\"") != std::string::npos);
    REQUIRE(json.find("\"modifiedAt\": \"2026-03-05T04:08:20Z\"") != std::string::npos);
}
```

Add `core/AnnotationSerializationTest.cpp` to the `slideio-viewer-core-tests` sources in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `AnnotationSerialization.h` does not exist and `Annotation` has no five-argument constructor.

- [ ] **Step 3: Add the five-argument `Annotation` constructor**

In `src/core/include/slideio/viewer/core/Annotation.h`, after the existing constructor declaration:

```cpp
    /// Restores an annotation exactly as stored, including its timestamps.
    /// Loading must not restamp createdAt/modifiedAt the way the constructor
    /// above does, and creation must be able to supply an author.
    Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry,
               AnnotationProperties properties, AnnotationMetadata metadata);
```

In `src/core/src/Annotation.cpp`:

```cpp
Annotation::Annotation(std::string id, AnnotationType type, AnnotationGeometry geometry,
                       AnnotationProperties properties, AnnotationMetadata metadata)
    : m_id(std::move(id))
    , m_type(type)
    , m_geometry(std::move(geometry))
    , m_properties(std::move(properties))
    , m_metadata(std::move(metadata))
{
}
```

- [ ] **Step 4: Write the document header**

Create `src/core/include/slideio/viewer/core/AnnotationDocument.h`:

```cpp
#pragma once

#include "slideio/viewer/core/Annotation.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// Bumped whenever the on-disk shape changes in a way an older build cannot
/// read correctly. A file carrying a higher number is refused, never
/// partially loaded -- see AnnotationSerialization.h.
inline constexpr int kCurrentSchemaVersion = 1;

/// Human-readable provenance, written so a hash-named file can be identified
/// by opening it. Never matched against: the slide identity check uses
/// `AnnotationDocument::slideId`.
struct SlideProvenance
{
    std::string fileName;
    std::string path;
    int64_t width = 0;
    int64_t height = 0;
};

/// One scene's annotations, as stored. The pair (slideId, sceneIndex) is the
/// key: computeSlideId is file-level by construction, so the scene index is
/// what keeps a multi-scene file's scenes apart.
struct AnnotationDocument
{
    int schemaVersion = kCurrentSchemaVersion;
    std::string slideId;
    int sceneIndex = 0;
    SlideProvenance slide;
    std::chrono::system_clock::time_point createdAt{};
    std::chrono::system_clock::time_point modifiedAt{};
    std::vector<Annotation> annotations;
};

} // namespace slideio::viewer::core
```

- [ ] **Step 5: Write the serializer header**

Create `src/core/include/slideio/viewer/core/AnnotationSerialization.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationDocument.h"

#include <string>
#include <string_view>

namespace slideio::viewer::core
{

/// Renders `document` as pretty-printed JSON with a trailing newline.
///
/// Returns an empty string when the document cannot be represented -- today
/// that means a non-finite coordinate, which JSON cannot carry and which
/// nlohmann would otherwise write as `null`, producing a file that will not
/// parse back. Callers must treat an empty result as a failed save rather than
/// writing it out.
std::string serializeAnnotationDocument(const AnnotationDocument& document);

} // namespace slideio::viewer::core
```

- [ ] **Step 6: Write the serializer**

Create `src/core/src/AnnotationSerialization.cpp`:

```cpp
#include "slideio/viewer/core/AnnotationSerialization.h"

#include "slideio/viewer/core/Iso8601.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <variant>

namespace slideio::viewer::core
{

namespace
{

using nlohmann::json;

constexpr const char* kRectangleTypeName = "rectangle";

bool isFinitePoint(const PointF& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool isRepresentable(const AnnotationGeometry& geometry)
{
    static_assert(std::variant_size_v<AnnotationGeometry> == 1,
                  "A new geometry alternative needs its own finiteness check and its own "
                  "JSON shape below; silently writing nulls corrupts the file.");
    const auto& rectangle = std::get<RectangleGeometry>(geometry);
    return isFinitePoint(rectangle.topLeft) && isFinitePoint(rectangle.bottomRight);
}

std::string formatColor(const Color& color)
{
    std::array<char, 16> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "#%02X%02X%02X%02X",
                  color.r, color.g, color.b, color.a);
    return std::string(buffer.data());
}

json pointToJson(const PointF& point)
{
    return json{{"x", point.x}, {"y", point.y}};
}

json geometryToJson(const AnnotationGeometry& geometry)
{
    static_assert(std::variant_size_v<AnnotationGeometry> == 1,
                  "A new geometry alternative needs its own JSON shape here.");
    const auto& rectangle = std::get<RectangleGeometry>(geometry);
    return json{
        {"type", kRectangleTypeName},
        {"topLeft", pointToJson(rectangle.topLeft)},
        {"bottomRight", pointToJson(rectangle.bottomRight)},
    };
}

const char* typeName(AnnotationType type)
{
    static_assert(static_cast<int>(AnnotationType::Rectangle) == 0,
                  "A new AnnotationType needs its own name here.");
    (void)type;
    return kRectangleTypeName;
}

json annotationToJson(const Annotation& annotation)
{
    const AnnotationProperties& properties = annotation.properties();
    const AnnotationMetadata& metadata = annotation.metadata();

    return json{
        {"id", annotation.id()},
        {"type", typeName(annotation.type())},
        {"geometry", geometryToJson(annotation.geometry())},
        {"properties", json{
            {"label", properties.label},
            {"classification", properties.classification},
            {"color", formatColor(properties.color)},
            {"lineWidth", properties.lineWidth},
            {"fillOpacity", properties.fillOpacity},
            {"notes", properties.notes},
        }},
        {"metadata", json{
            {"author", metadata.author},
            {"createdAt", formatIso8601Utc(metadata.createdAt)},
            {"modifiedAt", formatIso8601Utc(metadata.modifiedAt)},
        }},
    };
}

} // namespace

std::string serializeAnnotationDocument(const AnnotationDocument& document)
{
    for (const Annotation& annotation : document.annotations) {
        if (!isRepresentable(annotation.geometry())) {
            return {};
        }
    }

    json root{
        {"schemaVersion", document.schemaVersion},
        {"slideId", document.slideId},
        {"sceneIndex", document.sceneIndex},
        {"slide", json{
            {"fileName", document.slide.fileName},
            {"path", document.slide.path},
            {"width", document.slide.width},
            {"height", document.slide.height},
        }},
        {"createdAt", formatIso8601Utc(document.createdAt)},
        {"modifiedAt", formatIso8601Utc(document.modifiedAt)},
        {"annotations", json::array()},
    };

    for (const Annotation& annotation : document.annotations) {
        root["annotations"].push_back(annotationToJson(annotation));
    }

    return root.dump(2) + "\n";
}

} // namespace slideio::viewer::core
```

- [ ] **Step 7: Link nlohmann_json to core**

In the top-level `CMakeLists.txt`, beside the existing `find_package` calls:

```cmake
find_package(nlohmann_json REQUIRED)
```

In `src/core/CMakeLists.txt`, add `src/AnnotationSerialization.cpp` to the sources and append:

```cmake
# PRIVATE: nlohmann appears in no core public header, so core keeps its
# "links nothing" shape from every consumer's point of view, and still links
# no Qt.
target_link_libraries(slideio-viewer-core PRIVATE nlohmann_json::nlohmann_json)
```

- [ ] **Step 8: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS, all `[core][AnnotationSerialization]` cases green.

If `0.30000000000000004` is not found, nlohmann is not emitting shortest-round-trip doubles — do not loosen the test. `json::dump` uses Grisu2 and is exact by default; a failure here means the value was converted through a `float` somewhere.

- [ ] **Step 9: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationDocument.h \
        src/core/include/slideio/viewer/core/AnnotationSerialization.h \
        src/core/src/AnnotationSerialization.cpp \
        src/core/include/slideio/viewer/core/Annotation.h src/core/src/Annotation.cpp \
        src/core/CMakeLists.txt CMakeLists.txt \
        tests/core/AnnotationSerializationTest.cpp tests/CMakeLists.txt
git commit -m "Add the annotation document model and its JSON serializer"
```

---

### Task 3: The annotation document parser

**Files:**
- Modify: `src/core/include/slideio/viewer/core/AnnotationSerialization.h`
- Modify: `src/core/src/AnnotationSerialization.cpp`
- Test: `tests/core/AnnotationSerializationTest.cpp` (append)

**Interfaces:**
- Consumes: `serializeAnnotationDocument`, `parseIso8601Utc`, `AnnotationDocument`.
- Produces: `enum class ParseError`, `struct ParseResult`, `ParseResult parseAnnotationDocument(std::string_view)`.

**This is where the data-loss bugs live.** Every failure must be a value the caller is forced to look at, and no failure may leave a half-populated document that a caller might save back.

- [ ] **Step 1: Write the failing test**

Append to `tests/core/AnnotationSerializationTest.cpp`:

```cpp
TEST_CASE("a document round-trips through JSON unchanged", "[core][AnnotationSerialization]")
{
    const AnnotationDocument original = makeDocument();
    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(original));

    REQUIRE(result.error == ParseError::None);
    const AnnotationDocument& parsed = result.document;

    REQUIRE(parsed.schemaVersion == kCurrentSchemaVersion);
    REQUIRE(parsed.slideId == original.slideId);
    REQUIRE(parsed.sceneIndex == original.sceneIndex);
    REQUIRE(parsed.slide.fileName == original.slide.fileName);
    REQUIRE(parsed.slide.path == original.slide.path);
    REQUIRE(parsed.slide.width == original.slide.width);
    REQUIRE(parsed.slide.height == original.slide.height);
    REQUIRE(parsed.createdAt == original.createdAt);
    REQUIRE(parsed.modifiedAt == original.modifiedAt);
    REQUIRE(parsed.annotations.size() == 1);

    const Annotation& a = parsed.annotations.front();
    const Annotation& b = original.annotations.front();
    REQUIRE(a.id() == b.id());
    REQUIRE(a.type() == b.type());
    REQUIRE(a.properties().label == b.properties().label);
    REQUIRE(a.properties().classification == b.properties().classification);
    REQUIRE(a.properties().notes == b.properties().notes);
    REQUIRE(a.properties().lineWidth == b.properties().lineWidth);
    REQUIRE(a.properties().fillOpacity == b.properties().fillOpacity);
    REQUIRE(a.metadata().author == b.metadata().author);
    REQUIRE(a.metadata().createdAt == b.metadata().createdAt);
    REQUIRE(a.metadata().modifiedAt == b.metadata().modifiedAt);
}

TEST_CASE("a colour round-trips including a non-opaque alpha", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(makeDocument()));
    REQUIRE(result.error == ParseError::None);

    const Color color = result.document.annotations.front().properties().color;
    REQUIRE(color.r == 0x1F);
    REQUIRE(color.g == 0x77);
    REQUIRE(color.b == 0xB4);
    REQUIRE(color.a == 0x80);
}

TEST_CASE("an inverted rectangle round-trips with its corners as drawn",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{400.0, 300.0}, PointF{100.0, 50.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);

    const auto& rectangle = std::get<RectangleGeometry>(result.document.annotations.front().geometry());
    REQUIRE(rectangle.topLeft.x == 400.0);
    REQUIRE(rectangle.topLeft.y == 300.0);
    REQUIRE(rectangle.bottomRight.x == 100.0);
    REQUIRE(rectangle.bottomRight.y == 50.0);
}

TEST_CASE("a double needing full precision survives the round trip",
          "[core][AnnotationSerialization]")
{
    AnnotationDocument document;
    document.slideId = "id";
    const double awkward = 0.1 + 0.2;
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{awkward, 0.0}, PointF{1.0, 1.0}});

    const ParseResult result = parseAnnotationDocument(serializeAnnotationDocument(document));
    REQUIRE(result.error == ParseError::None);
    const auto& rectangle = std::get<RectangleGeometry>(result.document.annotations.front().geometry());
    REQUIRE(rectangle.topLeft.x == awkward);
}

TEST_CASE("malformed JSON is reported, not thrown", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument("{\"schemaVersion\": 1,");
    REQUIRE(result.error == ParseError::MalformedJson);
    REQUIRE_FALSE(result.message.empty());
    REQUIRE(result.document.annotations.empty());
}

TEST_CASE("an empty input is malformed, not an empty document", "[core][AnnotationSerialization]")
{
    REQUIRE(parseAnnotationDocument("").error == ParseError::MalformedJson);
}

TEST_CASE("JSON that is not an annotation document is rejected", "[core][AnnotationSerialization]")
{
    SECTION("an array at the root")
    {
        REQUIRE(parseAnnotationDocument("[]").error == ParseError::NotAnAnnotationDocument);
    }
    SECTION("an object with no schemaVersion")
    {
        REQUIRE(parseAnnotationDocument("{\"slideId\": \"x\"}").error
                == ParseError::NotAnAnnotationDocument);
    }
    SECTION("a schemaVersion that is not a number")
    {
        REQUIRE(parseAnnotationDocument("{\"schemaVersion\": \"1\"}").error
                == ParseError::NotAnAnnotationDocument);
    }
}

TEST_CASE("a newer schema version is refused outright", "[core][AnnotationSerialization]")
{
    // Loading what we understand and saving it back would silently drop
    // whatever the newer version added.
    const ParseResult result = parseAnnotationDocument(
        "{\"schemaVersion\": 2, \"slideId\": \"x\", \"sceneIndex\": 0, \"annotations\": []}");
    REQUIRE(result.error == ParseError::UnsupportedFutureVersion);
    REQUIRE(result.message.find('2') != std::string::npos);
}

TEST_CASE("a field of the wrong type is an InvalidField naming the field",
          "[core][AnnotationSerialization]")
{
    SECTION("annotations is not an array")
    {
        const ParseResult result = parseAnnotationDocument(
            "{\"schemaVersion\": 1, \"slideId\": \"x\", \"annotations\": {}}");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("annotations") != std::string::npos);
    }

    SECTION("a coordinate is a string")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
              {"id": "a1", "type": "rectangle",
               "geometry": {"type": "rectangle",
                            "topLeft": {"x": "nope", "y": 0.0},
                            "bottomRight": {"x": 1.0, "y": 1.0}}}
            ]})");
        REQUIRE(result.error == ParseError::InvalidField);
    }

    SECTION("an unknown geometry type")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
              {"id": "a1", "type": "hexagon",
               "geometry": {"type": "hexagon"}}
            ]})");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("hexagon") != std::string::npos);
    }

    SECTION("an annotation with no id")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
              {"type": "rectangle",
               "geometry": {"type": "rectangle",
                            "topLeft": {"x": 0.0, "y": 0.0},
                            "bottomRight": {"x": 1.0, "y": 1.0}}}
            ]})");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("id") != std::string::npos);
    }

    SECTION("an unparseable timestamp")
    {
        const ParseResult result = parseAnnotationDocument(R"({
            "schemaVersion": 1, "slideId": "x", "sceneIndex": 0,
            "createdAt": "yesterday", "annotations": []})");
        REQUIRE(result.error == ParseError::InvalidField);
        REQUIRE(result.message.find("createdAt") != std::string::npos);
    }
}

TEST_CASE("missing optional blocks fall back to defaults", "[core][AnnotationSerialization]")
{
    // A minimal hand-written file must load: properties, metadata, slide and
    // the timestamps are all omittable.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 2, "annotations": [
          {"id": "a1", "type": "rectangle",
           "geometry": {"type": "rectangle",
                        "topLeft": {"x": 0.0, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}}}
        ]})");

    REQUIRE(result.error == ParseError::None);
    REQUIRE(result.document.sceneIndex == 2);
    REQUIRE(result.document.annotations.size() == 1);

    const AnnotationProperties& properties = result.document.annotations.front().properties();
    REQUIRE(properties.color.r == 0xE6);
    REQUIRE(properties.lineWidth == 2.0f);
    REQUIRE(properties.label.empty());
}

TEST_CASE("unknown fields are dropped rather than preserved", "[core][AnnotationSerialization]")
{
    // Round-tripping unrecognised JSON would promise a forward compatibility
    // the version check already handles honestly.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0,
        "layers": ["not-implemented"], "annotations": []})");

    REQUIRE(result.error == ParseError::None);
    REQUIRE(serializeAnnotationDocument(result.document).find("layers") == std::string::npos);
}

TEST_CASE("a malformed colour is an InvalidField", "[core][AnnotationSerialization]")
{
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
          {"id": "a1", "type": "rectangle",
           "geometry": {"type": "rectangle",
                        "topLeft": {"x": 0.0, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}},
           "properties": {"color": "orange"}}
        ]})");
    REQUIRE(result.error == ParseError::InvalidField);
    REQUIRE(result.message.find("color") != std::string::npos);
}

TEST_CASE("a non-finite coordinate in a file is rejected on load",
          "[core][AnnotationSerialization]")
{
    // JSON has no NaN literal, but it has 1e400, which parses to infinity.
    const ParseResult result = parseAnnotationDocument(R"({
        "schemaVersion": 1, "slideId": "x", "sceneIndex": 0, "annotations": [
          {"id": "a1", "type": "rectangle",
           "geometry": {"type": "rectangle",
                        "topLeft": {"x": 1e400, "y": 0.0},
                        "bottomRight": {"x": 1.0, "y": 1.0}}}
        ]})");
    REQUIRE(result.error == ParseError::InvalidField);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `ParseResult`, `ParseError` and `parseAnnotationDocument` do not exist.

- [ ] **Step 3: Declare the parser**

Append to `src/core/include/slideio/viewer/core/AnnotationSerialization.h`, inside the namespace:

```cpp
enum class ParseError
{
    None,
    /// Not JSON at all, or truncated.
    MalformedJson,
    /// Valid JSON, but not this kind of document: no object at the root, or no
    /// numeric schemaVersion.
    NotAnAnnotationDocument,
    /// schemaVersion is higher than this build understands.
    UnsupportedFutureVersion,
    /// The shape is right but a value is not: wrong type, unknown enum,
    /// unparseable timestamp, non-finite coordinate.
    InvalidField,
};

struct ParseResult
{
    ParseError error = ParseError::None;
    /// Human-readable, naming the offending field. Shown to the user beside
    /// the file path, so it must say what is wrong, not merely that something is.
    std::string message;
    /// Meaningful only when error == None. On any failure this is a
    /// default-constructed document -- never a partially populated one, which a
    /// caller might otherwise save back over the user's file.
    AnnotationDocument document;
};

/// Parses what serializeAnnotationDocument produces. Never throws: nlohmann's
/// exceptions are caught and turned into a ParseError.
ParseResult parseAnnotationDocument(std::string_view json);
```

- [ ] **Step 4: Implement the parser**

In `src/core/src/AnnotationSerialization.cpp`, add to the anonymous namespace:

```cpp
ParseResult failure(ParseError error, std::string message)
{
    ParseResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

bool parseColor(const std::string& text, Color& out)
{
    if (text.size() != 9 || text[0] != '#') {
        return false;
    }
    unsigned int r = 0;
    unsigned int g = 0;
    unsigned int b = 0;
    unsigned int a = 0;
    char trailing = '\0';
    const int fields = std::sscanf(text.c_str(), "#%2x%2x%2x%2x%c", &r, &g, &b, &a, &trailing);
    if (fields != 4) {
        return false;
    }
    out = Color{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                static_cast<uint8_t>(b), static_cast<uint8_t>(a)};
    return true;
}

/// nlohmann's is_number_float() is false for an integer literal, and a
/// coordinate written as `0` is perfectly legal JSON, so both are accepted.
bool readDouble(const json& value, double& out)
{
    if (!value.is_number()) {
        return false;
    }
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool readPoint(const json& value, PointF& out)
{
    if (!value.is_object()) {
        return false;
    }
    return value.contains("x") && value.contains("y")
        && readDouble(value.at("x"), out.x) && readDouble(value.at("y"), out.y);
}

std::string readString(const json& parent, const char* key, const std::string& fallback)
{
    if (!parent.contains(key) || !parent.at(key).is_string()) {
        return fallback;
    }
    return parent.at(key).get<std::string>();
}

float readFloat(const json& parent, const char* key, float fallback)
{
    if (!parent.contains(key) || !parent.at(key).is_number()) {
        return fallback;
    }
    const double value = parent.at(key).get<double>();
    if (!std::isfinite(value)) {
        return fallback;
    }
    return static_cast<float>(value);
}

/// Absent is fine (the field is optional); present but unparseable is not,
/// because it means the file says something we cannot honour.
bool readTimestamp(const json& parent, const char* key,
                   std::chrono::system_clock::time_point& out, std::string& problem)
{
    if (!parent.contains(key)) {
        return true;
    }
    const json& value = parent.at(key);
    if (!value.is_string() || !parseIso8601Utc(value.get<std::string>(), out)) {
        problem = std::string(key) + " is not an ISO 8601 UTC timestamp";
        return false;
    }
    return true;
}

bool readGeometry(const json& value, AnnotationGeometry& out, std::string& problem)
{
    static_assert(std::variant_size_v<AnnotationGeometry> == 1,
                  "A new geometry alternative needs its own branch here.");

    if (!value.is_object() || !value.contains("type") || !value.at("type").is_string()) {
        problem = "geometry has no type";
        return false;
    }

    const std::string type = value.at("type").get<std::string>();
    if (type != kRectangleTypeName) {
        problem = "unknown geometry type '" + type + "'";
        return false;
    }

    RectangleGeometry rectangle;
    if (!value.contains("topLeft") || !readPoint(value.at("topLeft"), rectangle.topLeft)) {
        problem = "geometry.topLeft is not a finite point";
        return false;
    }
    if (!value.contains("bottomRight") || !readPoint(value.at("bottomRight"), rectangle.bottomRight)) {
        problem = "geometry.bottomRight is not a finite point";
        return false;
    }
    out = rectangle;
    return true;
}

bool readAnnotation(const json& value, Annotation& out, std::string& problem)
{
    if (!value.is_object()) {
        problem = "an entry of annotations is not an object";
        return false;
    }
    if (!value.contains("id") || !value.at("id").is_string()
        || value.at("id").get<std::string>().empty()) {
        problem = "an annotation has no id";
        return false;
    }

    AnnotationGeometry geometry;
    if (!value.contains("geometry") || !readGeometry(value.at("geometry"), geometry, problem)) {
        if (problem.empty()) {
            problem = "an annotation has no geometry";
        }
        return false;
    }

    AnnotationProperties properties;
    if (value.contains("properties") && value.at("properties").is_object()) {
        const json& source = value.at("properties");
        properties.label = readString(source, "label", properties.label);
        properties.classification = readString(source, "classification", properties.classification);
        properties.notes = readString(source, "notes", properties.notes);
        properties.lineWidth = readFloat(source, "lineWidth", properties.lineWidth);
        properties.fillOpacity = readFloat(source, "fillOpacity", properties.fillOpacity);
        if (source.contains("color")) {
            if (!source.at("color").is_string()
                || !parseColor(source.at("color").get<std::string>(), properties.color)) {
                problem = "properties.color is not #RRGGBBAA";
                return false;
            }
        }
    }

    AnnotationMetadata metadata;
    if (value.contains("metadata") && value.at("metadata").is_object()) {
        const json& source = value.at("metadata");
        metadata.author = readString(source, "author", metadata.author);
        if (!readTimestamp(source, "createdAt", metadata.createdAt, problem)
            || !readTimestamp(source, "modifiedAt", metadata.modifiedAt, problem)) {
            problem = "metadata." + problem;
            return false;
        }
    }

    // The type is derived from the geometry rather than read: the two are
    // independent in the constructor and a file must not be able to make them
    // disagree.
    out = Annotation(value.at("id").get<std::string>(), AnnotationType::Rectangle,
                     std::move(geometry), std::move(properties), std::move(metadata));
    return true;
}
```

Then the entry point, after `serializeAnnotationDocument`:

```cpp
ParseResult parseAnnotationDocument(std::string_view text)
{
    json root = json::parse(text, nullptr, false /*allow_exceptions*/);
    if (root.is_discarded()) {
        return failure(ParseError::MalformedJson, "the file is not valid JSON");
    }
    if (!root.is_object()) {
        return failure(ParseError::NotAnAnnotationDocument, "the root of the file is not an object");
    }
    if (!root.contains("schemaVersion") || !root.at("schemaVersion").is_number_integer()) {
        return failure(ParseError::NotAnAnnotationDocument,
                       "the file has no numeric schemaVersion, so it is not an annotation file");
    }

    const int version = root.at("schemaVersion").get<int>();
    if (version > kCurrentSchemaVersion) {
        return failure(ParseError::UnsupportedFutureVersion,
                       "the file uses schema version " + std::to_string(version)
                           + ", which is newer than this version of SlideIO Viewer understands ("
                           + std::to_string(kCurrentSchemaVersion) + ")");
    }

    AnnotationDocument document;
    document.schemaVersion = version;
    document.slideId = readString(root, "slideId", {});
    if (root.contains("sceneIndex")) {
        if (!root.at("sceneIndex").is_number_integer()) {
            return failure(ParseError::InvalidField, "sceneIndex is not an integer");
        }
        document.sceneIndex = root.at("sceneIndex").get<int>();
    }

    if (root.contains("slide") && root.at("slide").is_object()) {
        const json& slide = root.at("slide");
        document.slide.fileName = readString(slide, "fileName", {});
        document.slide.path = readString(slide, "path", {});
        if (slide.contains("width") && slide.at("width").is_number_integer()) {
            document.slide.width = slide.at("width").get<int64_t>();
        }
        if (slide.contains("height") && slide.at("height").is_number_integer()) {
            document.slide.height = slide.at("height").get<int64_t>();
        }
    }

    std::string problem;
    if (!readTimestamp(root, "createdAt", document.createdAt, problem)
        || !readTimestamp(root, "modifiedAt", document.modifiedAt, problem)) {
        return failure(ParseError::InvalidField, problem);
    }

    if (!root.contains("annotations")) {
        return failure(ParseError::InvalidField, "the file has no annotations array");
    }
    if (!root.at("annotations").is_array()) {
        return failure(ParseError::InvalidField, "annotations is not an array");
    }

    for (const json& entry : root.at("annotations")) {
        Annotation annotation("", AnnotationType::Rectangle,
                              RectangleGeometry{PointF{}, PointF{}});
        if (!readAnnotation(entry, annotation, problem)) {
            return failure(ParseError::InvalidField, problem);
        }
        document.annotations.push_back(std::move(annotation));
    }

    ParseResult result;
    result.document = std::move(document);
    return result;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationSerialization.h \
        src/core/src/AnnotationSerialization.cpp tests/core/AnnotationSerializationTest.cpp
git commit -m "Parse annotation documents, reporting every failure as a value"
```

---

### Task 4: The repository interface and the autosave policy

**Files:**
- Create: `src/core/include/slideio/viewer/core/AnnotationRepository.h`
- Create: `src/core/include/slideio/viewer/core/AutosavePolicy.h`
- Create: `src/core/src/AutosavePolicy.cpp`
- Test: `tests/core/AutosavePolicyTest.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AnnotationDocument`.
- Produces: `struct AnnotationKey`, `enum class LoadStatus`, `struct LoadResult`, `enum class SaveStatus`, `struct SaveResult`, `class IAnnotationRepository`, `bool shouldAutosave(...)`, and the two timing constants.

**Why the policy is a pure function:** no test suite creates a `QCoreApplication`, so a `QTimer` never fires in a test. The decision is extracted so it can be tested; the timer becomes a dumb clock that asks it.

- [ ] **Step 1: Write the failing test**

Create `tests/core/AutosavePolicyTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/core/AutosavePolicy.h"

#include <chrono>

using namespace slideio::viewer::core;
using namespace std::chrono_literals;

namespace
{

constexpr auto kDebounce = 2000ms;
constexpr auto kBackstop = 30000ms;

std::chrono::steady_clock::time_point base()
{
    return std::chrono::steady_clock::time_point{} + 1h;
}

} // namespace

TEST_CASE("a clean model is never saved", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(false, t0, t0, t0 + 60s, kDebounce, kBackstop));
}

TEST_CASE("a dirty model waits out the debounce", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    // Still being edited: a save now would write mid-drag and churn the disk.
    REQUIRE_FALSE(shouldAutosave(true, t0 + 1900ms, t0, t0 + 1900ms, kDebounce, kBackstop));
}

TEST_CASE("a dirty model saves once the debounce elapses", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE(shouldAutosave(true, t0, t0, t0 + 2000ms, kDebounce, kBackstop));
}

TEST_CASE("the debounce boundary is inclusive", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(true, t0, t0, t0 + 1999ms, kDebounce, kBackstop));
    REQUIRE(shouldAutosave(true, t0, t0, t0 + 2000ms, kDebounce, kBackstop));
}

TEST_CASE("continuous editing still saves at the backstop interval",
          "[core][AutosavePolicy]")
{
    // Someone dragging annotations without pause for half a minute: the
    // debounce never elapses, so without the backstop nothing would ever be
    // written. This is the case the backstop exists for.
    const auto t0 = base();
    const auto now = t0 + 30s;
    REQUIRE(shouldAutosave(true, now - 100ms, t0, now, kDebounce, kBackstop));
}

TEST_CASE("the backstop is measured from the last save, not the last mutation",
          "[core][AutosavePolicy]")
{
    const auto t0 = base();
    const auto lastSave = t0 + 25s;
    const auto now = t0 + 40s;   // 40s since t0, but only 15s since the save
    REQUIRE_FALSE(shouldAutosave(true, now - 100ms, lastSave, now, kDebounce, kBackstop));
}

TEST_CASE("a clean model is not saved even past the backstop", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(false, t0, t0, t0 + 10min, kDebounce, kBackstop));
}

TEST_CASE("a clock that has not advanced never triggers a save", "[core][AutosavePolicy]")
{
    const auto t0 = base();
    REQUIRE_FALSE(shouldAutosave(true, t0, t0, t0, kDebounce, kBackstop));
}
```

Add `core/AutosavePolicyTest.cpp` to the `slideio-viewer-core-tests` sources in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `AutosavePolicy.h` does not exist.

- [ ] **Step 3: Write the repository interface**

Create `src/core/include/slideio/viewer/core/AnnotationRepository.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationDocument.h"

#include <string>

namespace slideio::viewer::core
{

/// What identifies one annotation file.
///
/// The scene index is not decoration. computeSlideId is file-level by
/// construction -- its own documentation says it is "identical whichever scene
/// of a multi-scene file is open" -- while annotations live in the coordinate
/// space of one scene. Keyed on the slide id alone, a multi-scene file's scene
/// 1 would load scene 0's annotations at scene 0's coordinates.
struct AnnotationKey
{
    std::string slideId;
    int sceneIndex = 0;
};

enum class LoadStatus
{
    Loaded,
    /// No file for this key. The normal state of a slide nobody has annotated.
    NotFound,
    /// The file exists but could not be read: permissions, I/O error.
    Unreadable,
    /// The file was read but could not be parsed.
    Malformed,
    /// Written by a newer version of the application.
    UnsupportedVersion,
};

struct LoadResult
{
    LoadStatus status = LoadStatus::NotFound;
    std::string message;
    std::string path;
    AnnotationDocument document;
};

enum class SaveStatus
{
    Saved,
    /// The workspace directory is missing, or not writable.
    NotWritable,
    /// Everything else: a serialization refusal, a failed rename, a full disk.
    Failed,
};

struct SaveResult
{
    SaveStatus status = SaveStatus::Failed;
    std::string message;
    std::string path;
};

/// Reads and writes annotation documents. Implemented by
/// infra::JsonAnnotationRepository.
///
/// Stateless with respect to past calls: it cannot know that a previous load
/// failed, so the rule that "a file we could not parse is never a file we write
/// to" is enforced one layer up, in AnnotationPersistenceService.
class IAnnotationRepository
{
public:
    virtual ~IAnnotationRepository() = default;

    virtual LoadResult load(const AnnotationKey& key) = 0;
    virtual SaveResult save(const AnnotationDocument& document) = 0;

    /// Where `key` lives, whether or not anything is there. Exists so an error
    /// message can name a real path without the caller rebuilding the naming rule.
    [[nodiscard]] virtual std::string pathFor(const AnnotationKey& key) const = 0;
};

} // namespace slideio::viewer::core
```

- [ ] **Step 4: Write the autosave policy**

Create `src/core/include/slideio/viewer/core/AutosavePolicy.h`:

```cpp
#pragma once

#include <chrono>

namespace slideio::viewer::core
{

/// Quiet period after the last edit before a save is worth making.
inline constexpr std::chrono::milliseconds kAutosaveDebounce{2000};

/// Longest a dirty document may go unwritten while editing continues. Without
/// it, continuous editing resets the debounce forever and nothing is saved.
inline constexpr std::chrono::milliseconds kAutosaveBackstop{30000};

/// True when a dirty document should be written now.
///
/// Pure so it can be tested: no test suite creates a QCoreApplication, so a
/// QTimer never fires in a test. The timer calls this; the decision lives here.
bool shouldAutosave(bool dirty,
                    std::chrono::steady_clock::time_point lastMutation,
                    std::chrono::steady_clock::time_point lastSave,
                    std::chrono::steady_clock::time_point now,
                    std::chrono::milliseconds debounce,
                    std::chrono::milliseconds backstop);

} // namespace slideio::viewer::core
```

Create `src/core/src/AutosavePolicy.cpp`:

```cpp
#include "slideio/viewer/core/AutosavePolicy.h"

namespace slideio::viewer::core
{

bool shouldAutosave(bool dirty,
                    std::chrono::steady_clock::time_point lastMutation,
                    std::chrono::steady_clock::time_point lastSave,
                    std::chrono::steady_clock::time_point now,
                    std::chrono::milliseconds debounce,
                    std::chrono::milliseconds backstop)
{
    if (!dirty) {
        return false;
    }
    if (now - lastMutation >= debounce) {
        return true;
    }
    return now - lastSave >= backstop;
}

} // namespace slideio::viewer::core
```

Add `src/AutosavePolicy.cpp` to `src/core/CMakeLists.txt`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R core-tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/slideio/viewer/core/AnnotationRepository.h \
        src/core/include/slideio/viewer/core/AutosavePolicy.h \
        src/core/src/AutosavePolicy.cpp src/core/CMakeLists.txt \
        tests/core/AutosavePolicyTest.cpp tests/CMakeLists.txt
git commit -m "Add the annotation repository interface and the autosave policy"
```

---

### Task 5: `JsonAnnotationRepository` in infra

**Files:**
- Create: `src/infra/include/slideio/viewer/infra/JsonAnnotationRepository.h`
- Create: `src/infra/src/JsonAnnotationRepository.cpp`
- Test: `tests/infra/JsonAnnotationRepositoryTest.cpp`
- Modify: `src/infra/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::IAnnotationRepository`, `core::AnnotationKey`, `core::LoadResult`, `core::SaveResult`, `serializeAnnotationDocument`, `parseAnnotationDocument`.
- Produces: `infra::JsonAnnotationRepository(std::string workspaceRoot)` implementing the interface.

**Covers Review Focus item 3** (a read-only workspace reports `NotWritable` and never falls back).

- [ ] **Step 1: Write the failing test**

Create `tests/infra/JsonAnnotationRepositoryTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/infra/JsonAnnotationRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <string>

using namespace slideio::viewer::core;
using slideio::viewer::infra::JsonAnnotationRepository;

namespace
{

AnnotationDocument makeDocument(const std::string& slideId, int sceneIndex)
{
    AnnotationDocument document;
    document.slideId = slideId;
    document.sceneIndex = sceneIndex;
    document.slide.fileName = "case001_HE.svs";
    document.slide.path = "D:/data/case001_HE.svs";
    document.annotations.emplace_back("a1", AnnotationType::Rectangle,
                                      RectangleGeometry{PointF{10.0, 20.0}, PointF{110.0, 70.0}});
    return document;
}

} // namespace

TEST_CASE("the path carries both the slide id and the scene index",
          "[infra][JsonAnnotationRepository]")
{
    JsonAnnotationRepository repository("C:/workspace");

    const std::string scene0 = repository.pathFor({"a3f8c2e1b4d50697", 0});
    const std::string scene3 = repository.pathFor({"a3f8c2e1b4d50697", 3});

    REQUIRE(scene0.find("a3f8c2e1b4d50697.s0.annotations.json") != std::string::npos);
    REQUIRE(scene3.find("a3f8c2e1b4d50697.s3.annotations.json") != std::string::npos);
    REQUIRE(scene0 != scene3);
    REQUIRE(scene0.find("annotations") != std::string::npos);
}

TEST_CASE("an absent file is NotFound, not an error", "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    const LoadResult result = repository.load({"nothing-here", 0});
    REQUIRE(result.status == LoadStatus::NotFound);
    REQUIRE_FALSE(result.path.empty());
    REQUIRE(result.document.annotations.empty());
}

TEST_CASE("a saved document loads back", "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    const SaveResult saved = repository.save(makeDocument("a3f8c2e1b4d50697", 0));
    REQUIRE(saved.status == SaveStatus::Saved);

    const LoadResult loaded = repository.load({"a3f8c2e1b4d50697", 0});
    REQUIRE(loaded.status == LoadStatus::Loaded);
    REQUIRE(loaded.document.slideId == "a3f8c2e1b4d50697");
    REQUIRE(loaded.document.annotations.size() == 1);
    REQUIRE(loaded.document.annotations.front().id() == "a1");
}

TEST_CASE("two scenes of one slide do not collide", "[infra][JsonAnnotationRepository]")
{
    // The whole reason the key carries a scene index.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    AnnotationDocument scene0 = makeDocument("same-slide", 0);
    scene0.annotations.front() = Annotation("from-scene-0", AnnotationType::Rectangle,
                                            RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});
    AnnotationDocument scene1 = makeDocument("same-slide", 1);
    scene1.annotations.front() = Annotation("from-scene-1", AnnotationType::Rectangle,
                                            RectangleGeometry{PointF{0.0, 0.0}, PointF{1.0, 1.0}});

    REQUIRE(repository.save(scene0).status == SaveStatus::Saved);
    REQUIRE(repository.save(scene1).status == SaveStatus::Saved);

    REQUIRE(repository.load({"same-slide", 0}).document.annotations.front().id() == "from-scene-0");
    REQUIRE(repository.load({"same-slide", 1}).document.annotations.front().id() == "from-scene-1");
}

TEST_CASE("the workspace directory is created on first save, not before",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString annotationsDir = dir.path() + "/annotations";

    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE_FALSE(QFileInfo::exists(annotationsDir));

    repository.load({"anything", 0});
    REQUIRE_FALSE(QFileInfo::exists(annotationsDir));

    REQUIRE(repository.save(makeDocument("anything", 0)).status == SaveStatus::Saved);
    REQUIRE(QFileInfo::exists(annotationsDir));
}

TEST_CASE("a save leaves no temporary file behind", "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE(repository.save(makeDocument("a3f8c2e1b4d50697", 0)).status == SaveStatus::Saved);

    const QDir annotations(dir.path() + "/annotations");
    const QStringList leftovers = annotations.entryList(QStringList() << "*.tmp" << "*~",
                                                        QDir::Files | QDir::Hidden);
    REQUIRE(leftovers.isEmpty());
    REQUIRE(annotations.entryList(QDir::Files).size() == 1);
}

TEST_CASE("a save overwrites the previous document rather than appending",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    REQUIRE(repository.save(makeDocument("slide", 0)).status == SaveStatus::Saved);

    AnnotationDocument emptied = makeDocument("slide", 0);
    emptied.annotations.clear();
    REQUIRE(repository.save(emptied).status == SaveStatus::Saved);

    const LoadResult loaded = repository.load({"slide", 0});
    REQUIRE(loaded.status == LoadStatus::Loaded);
    REQUIRE(loaded.document.annotations.empty());
}

TEST_CASE("unparseable content on disk is Malformed and names the file",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE(QDir().mkpath(dir.path() + "/annotations"));

    const QString path = QString::fromStdString(repository.pathFor({"broken", 0}));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("{ this is not json");
    file.close();

    const LoadResult result = repository.load({"broken", 0});
    REQUIRE(result.status == LoadStatus::Malformed);
    REQUIRE(result.path == path.toStdString());
    REQUIRE_FALSE(result.message.empty());
    REQUIRE(result.document.annotations.empty());
}

TEST_CASE("a future schema version is reported as such, not as malformed",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());
    REQUIRE(QDir().mkpath(dir.path() + "/annotations"));

    QFile file(QString::fromStdString(repository.pathFor({"future", 0})));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(R"({"schemaVersion": 99, "slideId": "future", "sceneIndex": 0, "annotations": []})");
    file.close();

    REQUIRE(repository.load({"future", 0}).status == LoadStatus::UnsupportedVersion);
}

TEST_CASE("a document that cannot be serialized fails rather than writing a broken file",
          "[infra][JsonAnnotationRepository]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    JsonAnnotationRepository repository(dir.path().toStdString());

    AnnotationDocument document = makeDocument("slide", 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    document.annotations.front().setGeometry(RectangleGeometry{PointF{nan, 0.0}, PointF{1.0, 1.0}});

    const SaveResult result = repository.save(document);
    REQUIRE(result.status == SaveStatus::Failed);
    REQUIRE_FALSE(QFileInfo::exists(QString::fromStdString(result.path)));
}

TEST_CASE("a workspace that cannot be created is NotWritable and names the path",
          "[infra][JsonAnnotationRepository]")
{
    // A file where the workspace directory should be: mkpath cannot succeed,
    // and the repository must say so rather than quietly writing elsewhere.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString blocker = dir.path() + "/blocked";
    QFile file(blocker);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("not a directory");
    file.close();

    JsonAnnotationRepository repository(blocker.toStdString());
    const SaveResult result = repository.save(makeDocument("slide", 0));

    REQUIRE(result.status == SaveStatus::NotWritable);
    REQUIRE(result.message.find("blocked") != std::string::npos);
}
```

Add `infra/JsonAnnotationRepositoryTest.cpp` to the `slideio-viewer-infra-tests` sources in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `JsonAnnotationRepository.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/infra/include/slideio/viewer/infra/JsonAnnotationRepository.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationRepository.h"

#include <string>

namespace slideio::viewer::infra
{

/// Stores one JSON document per (slide, scene) under
/// `<workspaceRoot>/annotations/<slideId>.s<sceneIndex>.annotations.json`.
///
/// The directory is created on the first successful save, never on construction
/// or on a load: an installation that never annotates leaves nothing behind.
class JsonAnnotationRepository : public core::IAnnotationRepository
{
public:
    explicit JsonAnnotationRepository(std::string workspaceRoot);
    ~JsonAnnotationRepository() override;

    core::LoadResult load(const core::AnnotationKey& key) override;
    core::SaveResult save(const core::AnnotationDocument& document) override;
    [[nodiscard]] std::string pathFor(const core::AnnotationKey& key) const override;

    /// Where the repository is rooted. The workspace can change while the
    /// application runs, so a repository is replaced rather than re-rooted.
    [[nodiscard]] const std::string& workspaceRoot() const;

private:
    std::string m_workspaceRoot;
};

} // namespace slideio::viewer::infra
```

- [ ] **Step 4: Write the implementation**

Create `src/infra/src/JsonAnnotationRepository.cpp`:

```cpp
#include "slideio/viewer/infra/JsonAnnotationRepository.h"

#include "slideio/viewer/core/AnnotationSerialization.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

#include <utility>

namespace slideio::viewer::infra
{

namespace
{

QString annotationsDirectory(const std::string& root)
{
    return QString::fromStdString(root) + QStringLiteral("/annotations");
}

core::LoadResult loadFailure(core::LoadStatus status, std::string path, std::string message)
{
    core::LoadResult result;
    result.status = status;
    result.path = std::move(path);
    result.message = std::move(message);
    return result;
}

core::SaveResult saveFailure(core::SaveStatus status, std::string path, std::string message)
{
    core::SaveResult result;
    result.status = status;
    result.path = std::move(path);
    result.message = std::move(message);
    return result;
}

core::LoadStatus statusForParseError(core::ParseError error)
{
    switch (error) {
    case core::ParseError::UnsupportedFutureVersion:
        return core::LoadStatus::UnsupportedVersion;
    case core::ParseError::MalformedJson:
    case core::ParseError::NotAnAnnotationDocument:
    case core::ParseError::InvalidField:
        return core::LoadStatus::Malformed;
    case core::ParseError::None:
        break;
    }
    return core::LoadStatus::Loaded;
}

} // namespace

JsonAnnotationRepository::JsonAnnotationRepository(std::string workspaceRoot)
    : m_workspaceRoot(std::move(workspaceRoot))
{
}

JsonAnnotationRepository::~JsonAnnotationRepository() = default;

const std::string& JsonAnnotationRepository::workspaceRoot() const
{
    return m_workspaceRoot;
}

std::string JsonAnnotationRepository::pathFor(const core::AnnotationKey& key) const
{
    const QString name = QStringLiteral("%1.s%2.annotations.json")
                             .arg(QString::fromStdString(key.slideId))
                             .arg(key.sceneIndex);
    return (annotationsDirectory(m_workspaceRoot) + QLatin1Char('/') + name).toStdString();
}

core::LoadResult JsonAnnotationRepository::load(const core::AnnotationKey& key)
{
    const std::string path = pathFor(key);
    const QString qpath = QString::fromStdString(path);

    if (!QFileInfo::exists(qpath)) {
        return loadFailure(core::LoadStatus::NotFound, path, "no annotation file for this slide");
    }

    QFile file(qpath);
    if (!file.open(QIODevice::ReadOnly)) {
        return loadFailure(core::LoadStatus::Unreadable, path, file.errorString().toStdString());
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const core::ParseResult parsed =
        core::parseAnnotationDocument(std::string_view(bytes.constData(),
                                                       static_cast<std::size_t>(bytes.size())));
    if (parsed.error != core::ParseError::None) {
        return loadFailure(statusForParseError(parsed.error), path, parsed.message);
    }

    core::LoadResult result;
    result.status = core::LoadStatus::Loaded;
    result.path = path;
    result.document = parsed.document;
    return result;
}

core::SaveResult JsonAnnotationRepository::save(const core::AnnotationDocument& document)
{
    const std::string path = pathFor({document.slideId, document.sceneIndex});

    // Serialize before touching the filesystem: a document that cannot be
    // represented must not cost the user their previous file.
    const std::string json = core::serializeAnnotationDocument(document);
    if (json.empty()) {
        return saveFailure(core::SaveStatus::Failed, path,
                           "the annotations contain a coordinate JSON cannot represent");
    }

    const QString directory = annotationsDirectory(m_workspaceRoot);
    if (!QDir().mkpath(directory)) {
        return saveFailure(core::SaveStatus::NotWritable, path,
                           "cannot create the annotation folder at "
                               + directory.toStdString());
    }

    // QSaveFile writes to a temporary beside the target and renames on commit,
    // so an interrupted save leaves the previous file intact.
    QSaveFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly)) {
        return saveFailure(core::SaveStatus::NotWritable, path, file.errorString().toStdString());
    }
    if (file.write(json.data(), static_cast<qint64>(json.size())) != static_cast<qint64>(json.size())) {
        file.cancelWriting();
        return saveFailure(core::SaveStatus::Failed, path, file.errorString().toStdString());
    }
    if (!file.commit()) {
        return saveFailure(core::SaveStatus::Failed, path, file.errorString().toStdString());
    }

    core::SaveResult result;
    result.status = core::SaveStatus::Saved;
    result.path = path;
    return result;
}

} // namespace slideio::viewer::infra
```

Add `src/JsonAnnotationRepository.cpp` to `src/infra/CMakeLists.txt`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R infra-tests --output-on-failure
```

Expected: PASS. Run it through `ctest`, never as a bare executable.

- [ ] **Step 6: Commit**

```bash
git add src/infra/include/slideio/viewer/infra/JsonAnnotationRepository.h \
        src/infra/src/JsonAnnotationRepository.cpp src/infra/CMakeLists.txt \
        tests/infra/JsonAnnotationRepositoryTest.cpp tests/CMakeLists.txt
git commit -m "Store annotation documents as JSON per slide and scene"
```

---

### Task 6: `AnnotationModel` gains a load path and a default author

**Files:**
- Modify: `src/app/include/slideio/viewer/app/AnnotationModel.h`
- Modify: `src/app/src/AnnotationModel.cpp`
- Test: `tests/app/AnnotationModelTest.cpp` (append)

**Interfaces:**
- Consumes: the five-argument `core::Annotation` constructor from Task 2.
- Produces: `void insert(core::Annotation)`, `void replaceAll(std::vector<core::Annotation>)`, `void setDefaultAuthor(std::string)`, `const std::string& defaultAuthor() const`, signal `void modelReset()`.

**Why `add()` is not enough:** `add()` mints a fresh `QUuid` every time. Loading through it would churn every id on every save/load round trip, breaking anything that references an annotation across a reopen.

- [ ] **Step 1: Write the failing test**

Append to `tests/app/AnnotationModelTest.cpp`:

```cpp
TEST_CASE("insert keeps the id the annotation already carries", "[app][AnnotationModel]")
{
    AnnotationModel model;
    model.insert(core::Annotation("stored-id-42", core::AnnotationType::Rectangle,
                                  core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                          core::PointF{10.0, 10.0}}));

    REQUIRE(model.annotations().size() == 1);
    REQUIRE(model.annotations().front().id() == "stored-id-42");
    REQUIRE(model.find("stored-id-42") != nullptr);
}

TEST_CASE("insert preserves stored metadata rather than restamping it",
          "[app][AnnotationModel]")
{
    core::AnnotationMetadata metadata;
    metadata.author = "a.colleague";
    metadata.createdAt = std::chrono::system_clock::from_time_t(1000000);
    metadata.modifiedAt = std::chrono::system_clock::from_time_t(2000000);

    AnnotationModel model;
    model.setDefaultAuthor("me");
    model.insert(core::Annotation("a1", core::AnnotationType::Rectangle,
                                  core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                          core::PointF{1.0, 1.0}},
                                  core::AnnotationProperties{}, metadata));

    const core::Annotation* loaded = model.find("a1");
    REQUIRE(loaded != nullptr);
    // An annotation made by a colleague keeps their name when opened here.
    REQUIRE(loaded->metadata().author == "a.colleague");
    REQUIRE(loaded->metadata().createdAt == metadata.createdAt);
}

TEST_CASE("insert emits annotationAdded", "[app][AnnotationModel]")
{
    AnnotationModel model;
    int added = 0;
    QObject::connect(&model, &AnnotationModel::annotationAdded,
                     &model, [&added](const std::string&) { ++added; });

    model.insert(core::Annotation("a1", core::AnnotationType::Rectangle,
                                  core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                          core::PointF{1.0, 1.0}}));
    REQUIRE(added == 1);
}

TEST_CASE("replaceAll swaps the contents and emits one modelReset",
          "[app][AnnotationModel]")
{
    AnnotationModel model;
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int resets = 0;
    int adds = 0;
    QObject::connect(&model, &AnnotationModel::modelReset, &model, [&resets]() { ++resets; });
    QObject::connect(&model, &AnnotationModel::annotationAdded,
                     &model, [&adds](const std::string&) { ++adds; });

    std::vector<core::Annotation> loaded;
    loaded.emplace_back("x1", core::AnnotationType::Rectangle,
                        core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{2.0, 2.0}});
    loaded.emplace_back("x2", core::AnnotationType::Rectangle,
                        core::RectangleGeometry{core::PointF{3.0, 3.0}, core::PointF{4.0, 4.0}});
    model.replaceAll(std::move(loaded));

    REQUIRE(model.annotations().size() == 2);
    REQUIRE(model.annotations().front().id() == "x1");
    // One signal for the whole load, not one per annotation.
    REQUIRE(resets == 1);
    REQUIRE(adds == 0);
}

TEST_CASE("replaceAll clears the selection", "[app][AnnotationModel]")
{
    AnnotationModel model;
    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});
    model.setSelected(id);
    REQUIRE_FALSE(model.selectedId().empty());

    model.replaceAll({});
    REQUIRE(model.selectedId().empty());
    REQUIRE(model.annotations().empty());
}

TEST_CASE("replaceAll with an empty vector still emits modelReset",
          "[app][AnnotationModel]")
{
    // Loading a slide with no annotation file must still tell listeners to
    // repaint, or the previous slide's rectangles stay on screen.
    AnnotationModel model;
    int resets = 0;
    QObject::connect(&model, &AnnotationModel::modelReset, &model, [&resets]() { ++resets; });

    model.replaceAll({});
    REQUIRE(resets == 1);
}

TEST_CASE("add stamps the configured default author", "[app][AnnotationModel]")
{
    AnnotationModel model;
    model.setDefaultAuthor("s.melnikov");
    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});

    const core::Annotation* created = model.find(id);
    REQUIRE(created != nullptr);
    REQUIRE(created->metadata().author == "s.melnikov");
    // The timestamps are still stamped at creation.
    REQUIRE(created->metadata().createdAt.time_since_epoch().count() != 0);
}

TEST_CASE("add with no configured author leaves it empty", "[app][AnnotationModel]")
{
    AnnotationModel model;
    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});
    REQUIRE(model.find(id)->metadata().author.empty());
}
```

Ensure `tests/app/AnnotationModelTest.cpp` includes `<chrono>` and `<vector>`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `insert`, `replaceAll`, `setDefaultAuthor` and `modelReset` do not exist.

- [ ] **Step 3: Declare the additions**

In `src/app/include/slideio/viewer/app/AnnotationModel.h`, after `add`:

```cpp
    /// Stores `annotation` under the id it already carries, with its metadata
    /// exactly as given. This is the loading path: `add()` mints a fresh id and
    /// stamps the current time, which would churn every id on every save/load
    /// round trip.
    void insert(core::Annotation annotation);

    /// Replaces every annotation and clears the selection, emitting one
    /// modelReset() rather than one annotationAdded() per entry.
    void replaceAll(std::vector<core::Annotation> annotations);

    /// Stamped into the metadata of annotations created through add().
    /// Self-asserted and unverified: it identifies, it does not authenticate.
    void setDefaultAuthor(std::string author);
    [[nodiscard]] const std::string& defaultAuthor() const;
```

And in the `signals:` block:

```cpp
    /// The whole contents changed at once. Listeners must rebuild rather than
    /// track individual ids.
    void modelReset();
```

Add a `std::string m_defaultAuthor;` member.

- [ ] **Step 4: Implement**

In `src/app/src/AnnotationModel.cpp`:

```cpp
void AnnotationModel::insert(core::Annotation annotation)
{
    const std::string id = annotation.id();
    m_annotations.push_back(std::move(annotation));
    emit annotationAdded(id);
}

void AnnotationModel::replaceAll(std::vector<core::Annotation> annotations)
{
    m_annotations = std::move(annotations);

    // Cleared unconditionally rather than through clearSelection(), which emits
    // selectionChanged -- a listener would see a selection change referring to
    // a model it has not been told has been replaced yet.
    const bool hadSelection = !m_selectedId.empty();
    m_selectedId.clear();

    emit modelReset();
    if (hadSelection) {
        emit selectionChanged(m_selectedId);
    }
}

void AnnotationModel::setDefaultAuthor(std::string author)
{
    m_defaultAuthor = std::move(author);
}

const std::string& AnnotationModel::defaultAuthor() const
{
    return m_defaultAuthor;
}
```

And in `add()`, build the annotation with the five-argument constructor so the author is stamped. Replace the construction with:

```cpp
    core::AnnotationMetadata metadata;
    metadata.author = m_defaultAuthor;
    const auto now = std::chrono::system_clock::now();
    metadata.createdAt = now;
    metadata.modifiedAt = now;

    m_annotations.emplace_back(id, type, std::move(geometry),
                               core::AnnotationProperties{}, metadata);
```

(keeping whatever id generation and signal emission `add()` already does).

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R app-tests --output-on-failure
```

Expected: PASS. Through `ctest` only — a bare `slideio-viewer-app-tests.exe` pops a modal "Qt6Core.dll was not found" box.

- [ ] **Step 6: Commit**

```bash
git add src/app/include/slideio/viewer/app/AnnotationModel.h src/app/src/AnnotationModel.cpp \
        tests/app/AnnotationModelTest.cpp
git commit -m "Give AnnotationModel a loading path and a default author"
```

---

### Task 7: `AnnotationPersistenceService`

**Files:**
- Create: `src/app/include/slideio/viewer/app/AnnotationPersistenceService.h`
- Create: `src/app/src/AnnotationPersistenceService.cpp`
- Test: `tests/app/AnnotationPersistenceServiceTest.cpp`
- Modify: `src/app/CMakeLists.txt` (source **and** header, for AUTOMOC), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::IAnnotationRepository`, `core::AnnotationKey`, `core::SlideProvenance`, `AnnotationModel`, `core::shouldAutosave`.
- Produces: `AnnotationPersistenceService(core::IAnnotationRepository&, AnnotationModel&, QObject*)`, `beginSlide`, `flush`, `endSlide`, `isDirty`, `isActive`, `setMismatchResolver`, signals `loadFailed`, `saveFailed`, `activeChanged`.

**Covers Review Focus item 4** (a slide-ID mismatch reaches the resolver and never loads silently).

- [ ] **Step 1: Write the failing test**

Create `tests/app/AnnotationPersistenceServiceTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/app/AnnotationPersistenceService.h"

#include <map>
#include <string>
#include <vector>

using namespace slideio::viewer;
using app::AnnotationModel;
using app::AnnotationPersistenceService;

namespace
{

/// Records every call so the tests can assert on what the service did, and
/// never touches the filesystem.
class FakeRepository : public core::IAnnotationRepository
{
public:
    core::LoadResult load(const core::AnnotationKey& key) override
    {
        loadCount++;
        lastLoadedKey = key;
        return nextLoad;
    }

    core::SaveResult save(const core::AnnotationDocument& document) override
    {
        saveCount++;
        lastSaved = document;
        return nextSave;
    }

    [[nodiscard]] std::string pathFor(const core::AnnotationKey& key) const override
    {
        return "/fake/" + key.slideId + ".s" + std::to_string(key.sceneIndex) + ".json";
    }

    int loadCount = 0;
    int saveCount = 0;
    core::AnnotationKey lastLoadedKey;
    core::AnnotationDocument lastSaved;
    core::LoadResult nextLoad;
    core::SaveResult nextSave{core::SaveStatus::Saved, {}, "/fake/path"};
};

core::SlideProvenance provenance()
{
    core::SlideProvenance slide;
    slide.fileName = "case001_HE.svs";
    slide.path = "D:/data/case001_HE.svs";
    slide.width = 102400;
    slide.height = 76800;
    return slide;
}

core::Annotation rectangle(const std::string& id)
{
    return core::Annotation(id, core::AnnotationType::Rectangle,
                            core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
}

core::LoadResult loadedWith(const std::string& slideId, int sceneIndex,
                            std::vector<core::Annotation> annotations)
{
    core::LoadResult result;
    result.status = core::LoadStatus::Loaded;
    result.path = "/fake/path";
    result.document.slideId = slideId;
    result.document.sceneIndex = sceneIndex;
    result.document.annotations = std::move(annotations);
    return result;
}

} // namespace

TEST_CASE("beginSlide loads the key's annotations into the model",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad = loadedWith("slide-1", 0, {rectangle("a1"), rectangle("a2")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    REQUIRE(repository.loadCount == 1);
    REQUIRE(repository.lastLoadedKey.slideId == "slide-1");
    REQUIRE(model.annotations().size() == 2);
    REQUIRE(model.annotations().front().id() == "a1");
    REQUIRE(service.isActive());
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("a slide with no annotation file is active with an empty model",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad.status = core::LoadStatus::NotFound;

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"fresh-slide", 0}, provenance());

    REQUIRE(service.isActive());
    REQUIRE(model.annotations().empty());
}

TEST_CASE("loading does not mark the model dirty", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad = loadedWith("slide-1", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    REQUIRE_FALSE(service.isDirty());
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a mutation marks the model dirty", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    SECTION("adding")
    {
        model.add(core::AnnotationType::Rectangle,
                  core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
        REQUIRE(service.isDirty());
    }

    SECTION("moving")
    {
        const std::string id = model.add(core::AnnotationType::Rectangle,
                                         core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                                 core::PointF{1.0, 1.0}});
        service.flush();
        REQUIRE_FALSE(service.isDirty());
        model.setGeometry(id, core::RectangleGeometry{core::PointF{5.0, 5.0},
                                                      core::PointF{6.0, 6.0}});
        REQUIRE(service.isDirty());
    }

    SECTION("removing")
    {
        const std::string id = model.add(core::AnnotationType::Rectangle,
                                         core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                                 core::PointF{1.0, 1.0}});
        service.flush();
        model.remove(id);
        REQUIRE(service.isDirty());
    }
}

TEST_CASE("changing the selection does not mark the model dirty",
          "[app][AnnotationPersistenceService]")
{
    // Clicking an annotation must not trigger a save.
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    const std::string id = model.add(core::AnnotationType::Rectangle,
                                     core::RectangleGeometry{core::PointF{0.0, 0.0},
                                                             core::PointF{1.0, 1.0}});
    service.flush();
    REQUIRE_FALSE(service.isDirty());

    model.setSelected(id);
    model.clearSelection();
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("flush writes the model and clears the dirty flag",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-7", 2}, provenance());

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(service.flush().status == core::SaveStatus::Saved);

    REQUIRE(repository.saveCount == 1);
    REQUIRE(repository.lastSaved.slideId == "slide-7");
    REQUIRE(repository.lastSaved.sceneIndex == 2);
    REQUIRE(repository.lastSaved.slide.fileName == "case001_HE.svs");
    REQUIRE(repository.lastSaved.annotations.size() == 1);
    REQUIRE(repository.lastSaved.schemaVersion == core::kCurrentSchemaVersion);
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("a clean model is not written", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    service.flush();
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("deleting the last annotation still writes an empty document",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad = loadedWith("slide-1", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    model.remove("a1");
    REQUIRE(service.flush().status == core::SaveStatus::Saved);
    REQUIRE(repository.saveCount == 1);
    REQUIRE(repository.lastSaved.annotations.empty());
}

TEST_CASE("endSlide flushes and then goes inactive",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    service.endSlide();

    REQUIRE(repository.saveCount == 1);
    REQUIRE_FALSE(service.isActive());

    // A mutation after endSlide belongs to nobody and must not be written.
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{2.0, 2.0}, core::PointF{3.0, 3.0}});
    service.flush();
    REQUIRE(repository.saveCount == 1);
}

TEST_CASE("endSlide with no slide is safe", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    REQUIRE_FALSE(service.isActive());
    service.endSlide();
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("an empty slide id never becomes active", "[app][AnnotationPersistenceService]")
{
    // computeSlideId returns the same value for every unreadable file, so a
    // caller that cannot identify a slide must not look anything up.
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"", 0}, provenance());

    REQUIRE_FALSE(service.isActive());
    REQUIRE(repository.loadCount == 0);

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a malformed file leaves the service inactive and never saves over it",
          "[app][AnnotationPersistenceService]")
{
    // The single worst outcome this subsystem can produce is overwriting the
    // user's only copy with an empty document because we could not read theirs.
    FakeRepository repository;
    repository.nextLoad.status = core::LoadStatus::Malformed;
    repository.nextLoad.path = "/fake/broken.json";
    repository.nextLoad.message = "the file is not valid JSON";

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    QString failedPath;
    QString failedMessage;
    QObject::connect(&service, &AnnotationPersistenceService::loadFailed, &service,
                     [&](const QString& path, const QString& message) {
                         failedPath = path;
                         failedMessage = message;
                     });

    service.beginSlide({"broken-slide", 0}, provenance());

    REQUIRE_FALSE(service.isActive());
    REQUIRE(failedPath == "/fake/broken.json");
    REQUIRE(failedMessage.contains("valid JSON"));

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a future schema version also blocks saving",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextLoad.status = core::LoadStatus::UnsupportedVersion;

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"future-slide", 0}, provenance());

    REQUIRE_FALSE(service.isActive());
    service.flush();
    REQUIRE(repository.saveCount == 0);
}

TEST_CASE("a slide id mismatch is referred to the resolver, never resolved silently",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    // A file hand-copied from another slide: the name matched, the content does not.
    repository.nextLoad = loadedWith("a-different-slide", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    int resolverCalls = 0;

    SECTION("ignore")
    {
        service.setMismatchResolver(
            [&](const core::AnnotationDocument&, const core::SlideProvenance&) {
                ++resolverCalls;
                return AnnotationPersistenceService::MismatchChoice::Ignore;
            });
        service.beginSlide({"the-open-slide", 0}, provenance());

        REQUIRE(resolverCalls == 1);
        REQUIRE(model.annotations().empty());
        REQUIRE_FALSE(service.isActive());
    }

    SECTION("read-only loads but never saves")
    {
        service.setMismatchResolver(
            [&](const core::AnnotationDocument&, const core::SlideProvenance&) {
                ++resolverCalls;
                return AnnotationPersistenceService::MismatchChoice::ReadOnly;
            });
        service.beginSlide({"the-open-slide", 0}, provenance());

        REQUIRE(model.annotations().size() == 1);
        REQUIRE_FALSE(service.isActive());
        model.remove("a1");
        service.flush();
        REQUIRE(repository.saveCount == 0);
    }

    SECTION("re-associate adopts the open slide's id")
    {
        service.setMismatchResolver(
            [&](const core::AnnotationDocument&, const core::SlideProvenance&) {
                ++resolverCalls;
                return AnnotationPersistenceService::MismatchChoice::Reassociate;
            });
        service.beginSlide({"the-open-slide", 0}, provenance());

        REQUIRE(model.annotations().size() == 1);
        REQUIRE(service.isActive());

        model.add(core::AnnotationType::Rectangle,
                  core::RectangleGeometry{core::PointF{9.0, 9.0}, core::PointF{10.0, 10.0}});
        REQUIRE(service.flush().status == core::SaveStatus::Saved);
        REQUIRE(repository.lastSaved.slideId == "the-open-slide");
    }
}

TEST_CASE("with no resolver installed a mismatch is treated as read-only",
          "[app][AnnotationPersistenceService]")
{
    // The safe default: show the annotations, refuse to write.
    FakeRepository repository;
    repository.nextLoad = loadedWith("a-different-slide", 0, {rectangle("a1")});

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"the-open-slide", 0}, provenance());

    REQUIRE(model.annotations().size() == 1);
    REQUIRE_FALSE(service.isActive());
}

TEST_CASE("a failed save is reported and leaves the model dirty",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::NotWritable,
                                           "the folder is read-only", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());

    QString reportedPath;
    QObject::connect(&service, &AnnotationPersistenceService::saveFailed, &service,
                     [&](const QString& path, const QString&) { reportedPath = path; });

    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});
    REQUIRE(service.flush().status == core::SaveStatus::NotWritable);

    REQUIRE(reportedPath == "/fake/path");
    // Still dirty, so the next autosave tick retries rather than forgetting.
    REQUIRE(service.isDirty());
}

TEST_CASE("a save that fails while closing asks the resolver",
          "[app][AnnotationPersistenceService]")
{
    // openScene() calls closeSlide() internally, so this is the slide-switch
    // path. Failing silently here is how an hour of work disappears.
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::NotWritable,
                                           "the folder is read-only", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int asked = 0;
    int reportedCount = -1;
    service.setSaveFailureResolver(
        [&](const core::SaveResult& failure, int count) {
            ++asked;
            reportedCount = count;
            REQUIRE(failure.status == core::SaveStatus::NotWritable);
            return AnnotationPersistenceService::SaveFailureChoice::Discard;
        });

    service.endSlide();

    REQUIRE(asked == 1);
    // The dialog names how many annotations are at stake, so it must be told.
    REQUIRE(reportedCount == 1);
    REQUIRE_FALSE(service.isActive());
}

TEST_CASE("Retry re-attempts the save and succeeds once the cause is fixed",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::NotWritable, "read-only", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int asked = 0;
    service.setSaveFailureResolver(
        [&](const core::SaveResult&, int) {
            ++asked;
            // Standing in for the user choosing a writable folder.
            repository.nextSave = core::SaveResult{core::SaveStatus::Saved, {}, "/fake/path"};
            return AnnotationPersistenceService::SaveFailureChoice::Retry;
        });

    service.endSlide();

    REQUIRE(asked == 1);
    REQUIRE(repository.saveCount == 2);
    REQUIRE_FALSE(service.isDirty());
}

TEST_CASE("with no resolver a failing close still completes",
          "[app][AnnotationPersistenceService]")
{
    // Better to lose the annotations than to leave the application unable to
    // close a slide. The saveFailed signal still reports it.
    FakeRepository repository;
    repository.nextSave = core::SaveResult{core::SaveStatus::Failed, "disk full", "/fake/path"};

    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    service.endSlide();
    REQUIRE_FALSE(service.isActive());
}

TEST_CASE("a successful close never asks the resolver",
          "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);
    service.beginSlide({"slide-1", 0}, provenance());
    model.add(core::AnnotationType::Rectangle,
              core::RectangleGeometry{core::PointF{0.0, 0.0}, core::PointF{1.0, 1.0}});

    int asked = 0;
    service.setSaveFailureResolver([&](const core::SaveResult&, int) {
        ++asked;
        return AnnotationPersistenceService::SaveFailureChoice::Discard;
    });

    service.endSlide();
    REQUIRE(asked == 0);
}

TEST_CASE("activeChanged reports both directions", "[app][AnnotationPersistenceService]")
{
    FakeRepository repository;
    AnnotationModel model;
    AnnotationPersistenceService service(repository, model);

    std::vector<bool> states;
    QObject::connect(&service, &AnnotationPersistenceService::activeChanged, &service,
                     [&states](bool active) { states.push_back(active); });

    service.beginSlide({"slide-1", 0}, provenance());
    service.endSlide();

    REQUIRE(states == std::vector<bool>{true, false});
}
```

Add `app/AnnotationPersistenceServiceTest.cpp` to the `slideio-viewer-app-tests` sources in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `AnnotationPersistenceService.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/app/include/slideio/viewer/app/AnnotationPersistenceService.h`:

```cpp
#pragma once

#include "slideio/viewer/core/AnnotationRepository.h"

#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <string>

class QTimer;

namespace slideio::viewer::app
{

class AnnotationModel;

/// Owns when annotations are read and written, and whether writing is allowed
/// at all.
///
/// The governing rule lives here, not in the repository, which is stateless and
/// cannot know a previous load failed: **a file we could not parse is never a
/// file we write to.** A failed or read-only load leaves isActive() false, and
/// flush() on an inactive service is a no-op.
class AnnotationPersistenceService : public QObject
{
    Q_OBJECT

public:
    /// What to do about a file whose embedded slide id is not this slide's.
    enum class MismatchChoice
    {
        /// Show the annotations; never write. The default when no resolver is set.
        ReadOnly,
        /// Adopt the open slide's id and allow writing from now on.
        Reassociate,
        /// Discard them; leave the model empty and writing disabled.
        Ignore,
    };

    /// Asked to decide a mismatch. Takes the stored document and the open
    /// slide's provenance so the caller can show the user both names.
    /// Installed by the ui layer; the dialog does not belong in this layer.
    using MismatchResolver =
        std::function<MismatchChoice(const core::AnnotationDocument&, const core::SlideProvenance&)>;

    /// What to do when the flush inside endSlide() fails.
    enum class SaveFailureChoice
    {
        /// Try the same save again -- after the resolver has given the user a
        /// chance to fix the cause, such as choosing another folder.
        Retry,
        /// Proceed with the close and lose the annotations. The default when no
        /// resolver is installed, because a close that cannot be completed is
        /// worse than one that loses work the user was told about.
        Discard,
    };

    /// Asked when a save fails while a slide is closing. This is the dangerous
    /// one: openScene() calls closeSlide() internally, so this fires on a slide
    /// *switch*, which is exactly where work would otherwise disappear with no
    /// dialog in sight.
    using SaveFailureResolver = std::function<SaveFailureChoice(const core::SaveResult&, int annotationCount)>;

    AnnotationPersistenceService(core::IAnnotationRepository& repository,
                                 AnnotationModel& model,
                                 QObject* parent = nullptr);
    ~AnnotationPersistenceService() override;

    AnnotationPersistenceService(const AnnotationPersistenceService&) = delete;
    AnnotationPersistenceService& operator=(const AnnotationPersistenceService&) = delete;

    void setMismatchResolver(MismatchResolver resolver);
    void setSaveFailureResolver(SaveFailureResolver resolver);

    /// Loads this key into the model and begins tracking changes. An empty
    /// slideId never becomes active: computeSlideId yields the same value for
    /// every unidentifiable file, so a caller that cannot identify a slide must
    /// not look anything up.
    void beginSlide(const core::AnnotationKey& key, const core::SlideProvenance& provenance);

    /// Writes now, synchronously, if dirty and active. A no-op otherwise.
    core::SaveResult flush();

    /// flush(), then stop tracking. Safe with no slide open.
    core::SaveResult endSlide();

    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool isActive() const;

signals:
    void loadFailed(const QString& path, const QString& message);
    void saveFailed(const QString& path, const QString& message);
    /// Whether annotations can be created and written right now. The ui layer
    /// enables and disables the drawing tools from this.
    void activeChanged(bool active);

private:
    void markDirty();
    void onAutosaveTick();
    void setActive(bool active);

    core::IAnnotationRepository& m_repository;
    AnnotationModel& m_model;
    MismatchResolver m_mismatchResolver;
    SaveFailureResolver m_saveFailureResolver;
    QTimer* m_autosaveTimer = nullptr;

    core::AnnotationKey m_key;
    core::SlideProvenance m_provenance;
    std::chrono::system_clock::time_point m_createdAt{};
    bool m_active = false;
    bool m_dirty = false;
    bool m_loading = false;
    std::chrono::steady_clock::time_point m_lastMutation{};
    std::chrono::steady_clock::time_point m_lastSave{};
};

} // namespace slideio::viewer::app
```

- [ ] **Step 4: Write the implementation**

Create `src/app/src/AnnotationPersistenceService.cpp`:

```cpp
#include "slideio/viewer/app/AnnotationPersistenceService.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/core/AutosavePolicy.h"

#include <QTimer>

#include <utility>

namespace slideio::viewer::app
{

namespace
{

/// How often the timer asks the policy. Finer than the debounce so the debounce
/// boundary is honoured to within a tick, coarse enough to cost nothing.
constexpr int kAutosaveTickMs = 500;

} // namespace

AnnotationPersistenceService::AnnotationPersistenceService(core::IAnnotationRepository& repository,
                                                           AnnotationModel& model,
                                                           QObject* parent)
    : QObject(parent)
    , m_repository(repository)
    , m_model(model)
{
    // Selection is deliberately not connected: it is not persisted, and
    // clicking an annotation must not dirty the document or trigger a save.
    connect(&m_model, &AnnotationModel::annotationAdded, this,
            [this](const std::string&) { markDirty(); });
    connect(&m_model, &AnnotationModel::annotationRemoved, this,
            [this](const std::string&) { markDirty(); });
    connect(&m_model, &AnnotationModel::annotationChanged, this,
            [this](const std::string&) { markDirty(); });

    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(kAutosaveTickMs);
    connect(m_autosaveTimer, &QTimer::timeout, this, &AnnotationPersistenceService::onAutosaveTick);
}

AnnotationPersistenceService::~AnnotationPersistenceService() = default;

void AnnotationPersistenceService::setMismatchResolver(MismatchResolver resolver)
{
    m_mismatchResolver = std::move(resolver);
}

void AnnotationPersistenceService::setSaveFailureResolver(SaveFailureResolver resolver)
{
    m_saveFailureResolver = std::move(resolver);
}

void AnnotationPersistenceService::beginSlide(const core::AnnotationKey& key,
                                              const core::SlideProvenance& provenance)
{
    endSlide();

    m_key = key;
    m_provenance = provenance;
    m_createdAt = std::chrono::system_clock::now();
    m_dirty = false;
    m_lastMutation = std::chrono::steady_clock::now();
    m_lastSave = m_lastMutation;

    if (key.slideId.empty()) {
        // Every unidentifiable file produces the same id, so looking one up
        // would hand this slide another slide's annotations.
        m_model.replaceAll({});
        setActive(false);
        return;
    }

    const core::LoadResult loaded = m_repository.load(key);

    switch (loaded.status) {
    case core::LoadStatus::NotFound:
        m_loading = true;
        m_model.replaceAll({});
        m_loading = false;
        setActive(true);
        return;

    case core::LoadStatus::Unreadable:
    case core::LoadStatus::Malformed:
    case core::LoadStatus::UnsupportedVersion:
        m_loading = true;
        m_model.replaceAll({});
        m_loading = false;
        setActive(false);
        emit loadFailed(QString::fromStdString(loaded.path),
                        QString::fromStdString(loaded.message));
        return;

    case core::LoadStatus::Loaded:
        break;
    }

    bool writable = true;
    std::vector<core::Annotation> annotations = loaded.document.annotations;

    if (loaded.document.slideId != key.slideId) {
        const MismatchChoice choice =
            m_mismatchResolver ? m_mismatchResolver(loaded.document, provenance)
                               : MismatchChoice::ReadOnly;
        switch (choice) {
        case MismatchChoice::Ignore:
            annotations.clear();
            writable = false;
            break;
        case MismatchChoice::ReadOnly:
            writable = false;
            break;
        case MismatchChoice::Reassociate:
            // The document adopts this slide's id on the next save; m_key
            // already holds it.
            writable = true;
            break;
        }
    }

    if (loaded.document.createdAt.time_since_epoch().count() != 0) {
        m_createdAt = loaded.document.createdAt;
    }

    m_loading = true;
    m_model.replaceAll(std::move(annotations));
    m_loading = false;
    m_dirty = false;
    setActive(writable);
}

core::SaveResult AnnotationPersistenceService::flush()
{
    if (!m_active || !m_dirty) {
        core::SaveResult result;
        result.status = core::SaveStatus::Saved;
        result.path = m_active ? m_repository.pathFor(m_key) : std::string{};
        return result;
    }

    core::AnnotationDocument document;
    document.schemaVersion = core::kCurrentSchemaVersion;
    document.slideId = m_key.slideId;
    document.sceneIndex = m_key.sceneIndex;
    document.slide = m_provenance;
    document.createdAt = m_createdAt;
    document.modifiedAt = std::chrono::system_clock::now();
    document.annotations = m_model.annotations();

    const core::SaveResult result = m_repository.save(document);
    m_lastSave = std::chrono::steady_clock::now();

    if (result.status == core::SaveStatus::Saved) {
        m_dirty = false;
    } else {
        // Left dirty on purpose: the next tick retries rather than forgetting.
        emit saveFailed(QString::fromStdString(result.path),
                        QString::fromStdString(result.message));
    }
    return result;
}

core::SaveResult AnnotationPersistenceService::endSlide()
{
    core::SaveResult result = flush();

    // A close that fails silently is how a slide switch eats an hour of work.
    // The resolver gets a chance to fix the cause and say "try again"; without
    // one, the close proceeds rather than wedging the application.
    while (result.status != core::SaveStatus::Saved && m_saveFailureResolver) {
        const int count = static_cast<int>(m_model.annotations().size());
        if (m_saveFailureResolver(result, count) == SaveFailureChoice::Discard) {
            break;
        }
        result = flush();
    }

    m_autosaveTimer->stop();
    m_key = core::AnnotationKey{};
    m_provenance = core::SlideProvenance{};
    m_dirty = false;
    setActive(false);
    return result;
}

bool AnnotationPersistenceService::isDirty() const
{
    return m_dirty;
}

bool AnnotationPersistenceService::isActive() const
{
    return m_active;
}

void AnnotationPersistenceService::markDirty()
{
    if (m_loading || !m_active) {
        return;
    }
    m_dirty = true;
    m_lastMutation = std::chrono::steady_clock::now();
}

void AnnotationPersistenceService::onAutosaveTick()
{
    if (core::shouldAutosave(m_dirty, m_lastMutation, m_lastSave,
                             std::chrono::steady_clock::now(),
                             core::kAutosaveDebounce, core::kAutosaveBackstop)) {
        flush();
    }
}

void AnnotationPersistenceService::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (active) {
        m_autosaveTimer->start();
    } else {
        m_autosaveTimer->stop();
    }
    emit activeChanged(active);
}

} // namespace slideio::viewer::app
```

- [ ] **Step 5: Register the sources, including the header for AUTOMOC**

In `src/app/CMakeLists.txt`:

```cmake
add_library(slideio-viewer-app STATIC
    src/SlideViewerService.cpp
    src/AnnotationModel.cpp
    src/AnnotationPersistenceService.cpp
    # Listed so AUTOMOC runs moc on them: it does not scan headers that live in
    # a different directory from their .cpp.
    include/slideio/viewer/app/AnnotationModel.h
    include/slideio/viewer/app/AnnotationPersistenceService.h
)
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R app-tests --output-on-failure
```

Expected: PASS. If the link fails with unresolved `vtable for AnnotationPersistenceService` or missing `staticMetaObject`, the header was not added to the source list in Step 5.

- [ ] **Step 7: Commit**

```bash
git add src/app/include/slideio/viewer/app/AnnotationPersistenceService.h \
        src/app/src/AnnotationPersistenceService.cpp src/app/CMakeLists.txt \
        tests/app/AnnotationPersistenceServiceTest.cpp tests/CMakeLists.txt
git commit -m "Add the annotation persistence service with its save and load policy"
```

---

### Task 8: Workspace settings, the preferences dialog, and the folder action

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/AnnotationSettings.h`
- Create: `src/ui/src/AnnotationSettings.cpp`
- Create: `src/ui/include/slideio/viewer/ui/PreferencesDialog.h`
- Create: `src/ui/src/PreferencesDialog.cpp`
- Modify: `src/ui/include/slideio/viewer/ui/AppPaths.h`, `src/ui/src/AppPaths.cpp`
- Test: `tests/ui/AnnotationSettingsTest.cpp`
- Modify: `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `QString defaultAnnotationWorkspaceDirectory()`, `class AnnotationSettings` with `workspaceDirectory()`, `setWorkspaceDirectory(QString)`, `userName()`, `setUserName(QString)`, and `class PreferencesDialog`.

**`PreferencesDialog` is a widget, so it gets no test** — no suite creates a `QApplication`. `AnnotationSettings` is deliberately separate from it for exactly that reason: the resolution logic is testable, the dialog is three widgets over it.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/AnnotationSettingsTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "slideio/viewer/ui/AnnotationSettings.h"
#include "slideio/viewer/ui/AppPaths.h"

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

using slideio::viewer::ui::AnnotationSettings;

TEST_CASE("an unconfigured workspace resolves to the default", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    REQUIRE(settings.workspaceDirectory()
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
}

TEST_CASE("the default workspace is not the log directory", "[ui][AnnotationSettings]")
{
    // Logs are diagnostics and belong in app-local data; this is the user's
    // work and belongs somewhere they can find and back up.
    REQUIRE(slideio::viewer::ui::defaultAnnotationWorkspaceDirectory()
            != slideio::viewer::ui::logDirectory());
}

TEST_CASE("a configured workspace is returned verbatim", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setWorkspaceDirectory("D:/pathology/annotations");
    REQUIRE(settings.workspaceDirectory() == "D:/pathology/annotations");
}

TEST_CASE("the workspace survives across sessions", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath("settings.ini");

    {
        AnnotationSettings settings(ini);
        settings.setWorkspaceDirectory("D:/pathology/annotations");
        settings.setUserName("s.melnikov");
    }

    AnnotationSettings reopened(ini);
    REQUIRE(reopened.workspaceDirectory() == "D:/pathology/annotations");
    REQUIRE(reopened.userName() == "s.melnikov");
}

TEST_CASE("clearing the workspace falls back to the default", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setWorkspaceDirectory("D:/pathology/annotations");
    settings.setWorkspaceDirectory("");
    REQUIRE(settings.workspaceDirectory()
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
}

TEST_CASE("a workspace of only whitespace is treated as unset", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setWorkspaceDirectory("   ");
    REQUIRE(settings.workspaceDirectory()
            == slideio::viewer::ui::defaultAnnotationWorkspaceDirectory());
}

TEST_CASE("no user name is configured by default", "[ui][AnnotationSettings]")
{
    // FR-USER-01 gates annotation creation on this being set.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    REQUIRE(settings.userName().isEmpty());
    REQUIRE_FALSE(settings.hasUserName());
}

TEST_CASE("a user name of only whitespace does not count as configured",
          "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setUserName("   ");
    REQUIRE_FALSE(settings.hasUserName());
}

TEST_CASE("a user name is trimmed on the way in", "[ui][AnnotationSettings]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    AnnotationSettings settings(dir.filePath("settings.ini"));

    settings.setUserName("  s.melnikov  ");
    REQUIRE(settings.userName() == "s.melnikov");
    REQUIRE(settings.hasUserName());
}
```

Add `ui/AnnotationSettingsTest.cpp` to the `slideio-viewer-ui-tests` sources in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/build --config Release
```

Expected: FAIL to compile — `AnnotationSettings.h` does not exist.

- [ ] **Step 3: Add the default workspace path**

In `src/ui/include/slideio/viewer/ui/AppPaths.h`, after `logFilePath()`:

```cpp
// Default directory for the user's annotation workspace:
//   Windows: %USERPROFILE%\Documents\SlideIO Viewer
//   macOS:   ~/Documents/SlideIO Viewer
//   Linux:   $XDG_DOCUMENTS_DIR/SlideIO Viewer
//
// Deliberately NOT an application-data location. Slides often sit on read-only
// institutional storage, so annotations cannot live beside them; application
// data is writable but opaque, and a user who cannot find their annotations
// cannot back them up or send them to a colleague.
QString defaultAnnotationWorkspaceDirectory();
```

In `src/ui/src/AppPaths.cpp`:

```cpp
QString defaultAnnotationWorkspaceDirectory()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents + QStringLiteral("/") + QCoreApplication::applicationName();
}
```

Add `#include <QCoreApplication>` to `AppPaths.cpp`.

- [ ] **Step 4: Write `AnnotationSettings`**

Create `src/ui/include/slideio/viewer/ui/AnnotationSettings.h`:

```cpp
#pragma once

#include <QString>

#include <memory>

class QSettings;

namespace slideio::viewer::ui
{

/// The two preferences annotation persistence needs: where files go, and whose
/// name goes on them.
///
/// Separate from PreferencesDialog so the resolution rules are testable -- no
/// test suite creates a QApplication, so no test can construct the dialog.
class AnnotationSettings
{
public:
    /// Uses the application's own QSettings store.
    AnnotationSettings();
    /// Uses an explicit INI file. For tests.
    explicit AnnotationSettings(const QString& iniFilePath);
    ~AnnotationSettings();

    AnnotationSettings(const AnnotationSettings&) = delete;
    AnnotationSettings& operator=(const AnnotationSettings&) = delete;

    /// The configured workspace, or defaultAnnotationWorkspaceDirectory() when
    /// nothing usable is stored. Never empty.
    [[nodiscard]] QString workspaceDirectory() const;

    /// An empty or whitespace-only path clears the setting rather than storing
    /// one that resolves to nothing.
    void setWorkspaceDirectory(const QString& path);

    /// Stamped into every annotation created from now on. Self-asserted and
    /// unverified: it identifies, it does not authenticate.
    [[nodiscard]] QString userName() const;
    void setUserName(const QString& name);

    /// FR-USER-01: annotation creation is blocked until this is true.
    [[nodiscard]] bool hasUserName() const;

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace slideio::viewer::ui
```

Create `src/ui/src/AnnotationSettings.cpp`:

```cpp
#include "slideio/viewer/ui/AnnotationSettings.h"

#include "slideio/viewer/ui/AppPaths.h"

#include <QSettings>

namespace slideio::viewer::ui
{

namespace
{

constexpr const char* kWorkspaceKey = "annotations/workspaceDirectory";
constexpr const char* kUserNameKey = "annotations/userName";

} // namespace

AnnotationSettings::AnnotationSettings()
    : m_settings(std::make_unique<QSettings>())
{
}

AnnotationSettings::AnnotationSettings(const QString& iniFilePath)
    : m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

AnnotationSettings::~AnnotationSettings() = default;

QString AnnotationSettings::workspaceDirectory() const
{
    const QString stored = m_settings->value(kWorkspaceKey).toString().trimmed();
    if (stored.isEmpty()) {
        return defaultAnnotationWorkspaceDirectory();
    }
    return stored;
}

void AnnotationSettings::setWorkspaceDirectory(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        m_settings->remove(kWorkspaceKey);
    } else {
        m_settings->setValue(kWorkspaceKey, trimmed);
    }
    m_settings->sync();
}

QString AnnotationSettings::userName() const
{
    return m_settings->value(kUserNameKey).toString().trimmed();
}

void AnnotationSettings::setUserName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        m_settings->remove(kUserNameKey);
    } else {
        m_settings->setValue(kUserNameKey, trimmed);
    }
    m_settings->sync();
}

bool AnnotationSettings::hasUserName() const
{
    return !userName().isEmpty();
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 5: Write the preferences dialog**

Create `src/ui/include/slideio/viewer/ui/PreferencesDialog.h`:

```cpp
#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

namespace slideio::viewer::ui
{

/// The application's first preferences dialog: the annotation workspace and the
/// user name. Built to be extended; it holds only what E1 needs.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                      QWidget* parent = nullptr);

    [[nodiscard]] QString workspaceDirectory() const;
    [[nodiscard]] QString userName() const;

private:
    void browseForWorkspace();

    QLineEdit* m_workspaceEdit = nullptr;
    QLineEdit* m_userNameEdit = nullptr;
};

} // namespace slideio::viewer::ui
```

Create `src/ui/src/PreferencesDialog.cpp`:

```cpp
#include "slideio/viewer/ui/PreferencesDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

PreferencesDialog::PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    m_workspaceEdit = new QLineEdit(workspaceDirectory, this);
    m_userNameEdit = new QLineEdit(userName, this);
    m_userNameEdit->setPlaceholderText(tr("Required before creating annotations"));

    auto* browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, &PreferencesDialog::browseForWorkspace);

    auto* workspaceRow = new QHBoxLayout;
    workspaceRow->addWidget(m_workspaceEdit, 1);
    workspaceRow->addWidget(browse);

    auto* form = new QFormLayout;
    form->addRow(tr("Annotation workspace:"), workspaceRow);
    form->addRow(tr("Your name:"), m_userNameEdit);

    // Said plainly, because the alternative -- moving a user's files as a side
    // effect of changing a setting -- is worse than leaving them where they are.
    auto* note = new QLabel(
        tr("Annotations already saved stay where they are. Changing this folder "
           "affects only annotations saved from now on."),
        this);
    note->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

QString PreferencesDialog::workspaceDirectory() const
{
    return m_workspaceEdit->text().trimmed();
}

QString PreferencesDialog::userName() const
{
    return m_userNameEdit->text().trimmed();
}

void PreferencesDialog::browseForWorkspace()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Choose the annotation workspace folder"), m_workspaceEdit->text());
    if (!chosen.isEmpty()) {
        m_workspaceEdit->setText(chosen);
    }
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 6: Register the sources**

In `src/ui/CMakeLists.txt`, add to the `slideio-viewer-ui` sources:

```cmake
    src/AnnotationSettings.cpp
    src/PreferencesDialog.cpp
```

`src/ui/CMakeLists.txt` globs its `Q_OBJECT` headers, so `PreferencesDialog.h` needs no explicit entry — confirm the glob is present before relying on it; if it is not, add `include/slideio/viewer/ui/PreferencesDialog.h` explicitly.

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cmake --build build/build --config Release
ctest --test-dir build/build -C Release -R ui-tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/AnnotationSettings.h src/ui/src/AnnotationSettings.cpp \
        src/ui/include/slideio/viewer/ui/PreferencesDialog.h src/ui/src/PreferencesDialog.cpp \
        src/ui/include/slideio/viewer/ui/AppPaths.h src/ui/src/AppPaths.cpp \
        src/ui/CMakeLists.txt tests/ui/AnnotationSettingsTest.cpp tests/CMakeLists.txt
git commit -m "Add annotation workspace settings and a preferences dialog"
```

---

### Task 9: `ViewportWidget` lifecycle hooks and the two persistence gates

**Files:**
- Modify: `src/ui/include/slideio/viewer/ui/ViewportWidget.h`
- Modify: `src/ui/src/ViewportWidget.cpp` (`closeSlide` at `:2248`, `installSceneOpenResult` at `:2354`)

**Interfaces:**
- Consumes: `app::AnnotationPersistenceService`, `core::AnnotationKey`, `core::SlideProvenance`.
- Produces: `void setPersistenceService(app::AnnotationPersistenceService*)`, signal `void annotationsAvailableChanged(bool available, const QString& reason)`.

**Covers Review Focus items 1 and 2.** This task has no unit tests — no suite can construct a widget. It is verified by the whole-branch review and by the manual checks in Step 5.

- [ ] **Step 1: Declare the hook and the signal**

In `src/ui/include/slideio/viewer/ui/ViewportWidget.h`, forward-declare the service alongside the existing `namespace app { class AnnotationModel; }` and add to the public section:

```cpp
    /// Non-owning. MainWindow owns the service; the widget only calls it at the
    /// two points in the slide lifecycle where the ordering matters.
    void setPersistenceService(app::AnnotationPersistenceService* service);
```

And to `signals:`:

```cpp
    /// Whether annotations can be created on what is currently displayed.
    /// `reason` is empty when they can, and names the obstacle when they cannot.
    void annotationsAvailableChanged(bool available, const QString& reason);
```

Add `app::AnnotationPersistenceService* persistence = nullptr;` to the `Impl` struct.

- [ ] **Step 2: Flush before the reset in `closeSlide`**

In `src/ui/src/ViewportWidget.cpp`, make this the **first** statement of `closeSlide()` (`:2248`), before anything else in the body:

```cpp
void ViewportWidget::closeSlide()
{
    // Before everything, and specifically before resetAnnotationState() below:
    // openScene() calls closeSlide() internally, so this is also the flush that
    // runs on a slide switch and on reopenCurrentScene() -- which is what keeps
    // an ICC profile change from destroying the slide's annotations.
    if (m_impl->persistence != nullptr) {
        m_impl->persistence->endSlide();
    }

    // ... existing body, unchanged, including resetAnnotationState() ...
```

- [ ] **Step 3: Load after the slide id is known in `installSceneOpenResult`**

In `installSceneOpenResult`, **after** `m_impl->currentSlideId = std::move(result.slideId);` (`:2395`) and after the slide info is installed, add:

```cpp
    if (m_impl->persistence != nullptr) {
        // Associated images compute the file's slide id with sceneIndex 0, so a
        // rectangle drawn on a label would be written into scene 0's file and
        // later painted over the tissue. An empty id means the slide could not
        // be identified, and computeSlideId yields the same value for every
        // such file.
        const bool auxiliary = result.isAuxImage;
        const bool identified = !m_impl->currentSlideId.empty();

        if (auxiliary || !identified) {
            m_impl->persistence->endSlide();
            const QString reason = auxiliary
                ? tr("Annotations are not available on associated images.")
                : tr("This slide could not be identified, so annotations cannot be saved.");
            emit annotationsAvailableChanged(false, reason);
        } else {
            core::SlideProvenance provenance;
            provenance.path = m_impl->currentFilePath;
            provenance.fileName = QFileInfo(QString::fromStdString(m_impl->currentFilePath))
                                      .fileName().toStdString();
            provenance.width = m_impl->slideInfo.width;
            provenance.height = m_impl->slideInfo.height;

            m_impl->persistence->beginSlide({m_impl->currentSlideId, m_impl->currentSceneIndex},
                                            provenance);
            emit annotationsAvailableChanged(m_impl->persistence->isActive(), QString());
        }
    }
```

`core::SlideInfo` carries flat `width` and `height` for the open scene (`Types.h:290-291`), so those two reads are correct as written — there is no per-scene lookup to do. Add `#include <QFileInfo>` and the service header to the includes.

- [ ] **Step 4: Implement the setter**

```cpp
void ViewportWidget::setPersistenceService(app::AnnotationPersistenceService* service)
{
    m_impl->persistence = service;
}
```

- [ ] **Step 5: Verify by hand in the running application**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
```

Run `build/install/release/bin/slideio-viewer.exe` and check all four:

1. Open a slide, draw two rectangles, close the slide, reopen it — both rectangles come back.
2. With rectangles on screen, change the slide's ICC profile. The slide reopens; the rectangles are still there. **This is follow-up item 4; it is the reason this task exists.**
3. Open an associated image (label or macro). The Rectangle tool is disabled.
4. Confirm the annotation file exists at `<Documents>/SlideIO Viewer/annotations/<id>.s0.annotations.json` and opens in a text editor as readable JSON naming the slide.

- [ ] **Step 6: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/ViewportWidget.h src/ui/src/ViewportWidget.cpp
git commit -m "Save annotations on slide close and load them on open"
```

---

### Task 10: `MainWindow` wiring, the menu actions, and the identity gate

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`

**Interfaces:**
- Consumes: everything from Tasks 5–9.
- Produces: nothing further.

**Covers FR-USER-01** — annotation creation blocked until a user name is configured, prompted on the first annotation attempt rather than at startup.

- [ ] **Step 1: Construct and wire the stack**

In `MainWindow`'s `Impl`, add members:

```cpp
    std::unique_ptr<AnnotationSettings> annotationSettings;
    std::unique_ptr<infra::JsonAnnotationRepository> annotationRepository;
    std::unique_ptr<app::AnnotationPersistenceService> annotationPersistence;
    QAction* preferencesAction = nullptr;
    QAction* openAnnotationsFolderAction = nullptr;
```

Construct the settings once, then build the repository and service through a
method that can run again later — changing the workspace in Preferences
replaces the repository, and the service has to be rebuilt on top of it:

```cpp
    annotationSettings = std::make_unique<AnnotationSettings>();
    viewportWidget->annotationModel()->setDefaultAuthor(
        annotationSettings->userName().toStdString());
    rebuildAnnotationPersistence();
```

Declare `void rebuildAnnotationPersistence();` on `MainWindow::Impl` and define
it to hold everything from here through Step 5, so both the initial
construction and a workspace change go through one path:

```cpp
void MainWindow::Impl::rebuildAnnotationPersistence()
{
    annotationRepository = std::make_unique<infra::JsonAnnotationRepository>(
        annotationSettings->workspaceDirectory().toStdString());
    annotationPersistence = std::make_unique<app::AnnotationPersistenceService>(
        *annotationRepository, *viewportWidget->annotationModel());
    viewportWidget->setPersistenceService(annotationPersistence.get());

    // Steps 2 to 5 below go here: the two resolvers, the two failure
    // reporters, and the activeChanged connection.
}
```

The `annotationsAvailableChanged` connection in Step 5 is on `viewportWidget`,
not on the service, so it is made **once** at construction rather than inside
this method — connecting it again on every workspace change would fire the
handler twice per signal.

- [ ] **Step 2: Install the mismatch resolver**

```cpp
    annotationPersistence->setMismatchResolver(
        [this](const core::AnnotationDocument& stored,
               const core::SlideProvenance& current) {
            QMessageBox box(m_impl->owner);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(tr("Annotations may belong to another slide"));
            box.setText(tr("The stored annotations were saved for a different slide."));
            // Both names, so the user has something to judge by. A matching id
            // is not proof of provenance, and a mismatch is not proof of error:
            // a re-exported slide has a new content hash and the same tissue.
            box.setInformativeText(tr("Saved for: %1\nCurrently open: %2")
                                       .arg(QString::fromStdString(stored.slide.fileName),
                                            QString::fromStdString(current.fileName)));
            QPushButton* readOnly = box.addButton(tr("Open Read-Only"), QMessageBox::AcceptRole);
            QPushButton* reassociate = box.addButton(tr("Use for This Slide"),
                                                     QMessageBox::DestructiveRole);
            box.addButton(tr("Ignore Them"), QMessageBox::RejectRole);
            box.setDefaultButton(readOnly);
            box.exec();

            using Choice = app::AnnotationPersistenceService::MismatchChoice;
            if (box.clickedButton() == reassociate) {
                return Choice::Reassociate;
            }
            if (box.clickedButton() == readOnly) {
                return Choice::ReadOnly;
            }
            return Choice::Ignore;
        });
```

- [ ] **Step 3: Install the close-failure resolver**

```cpp
    annotationPersistence->setSaveFailureResolver(
        [this](const core::SaveResult& failure, int annotationCount) {
            using Choice = app::AnnotationPersistenceService::SaveFailureChoice;

            QMessageBox box(m_impl->owner);
            box.setIcon(QMessageBox::Critical);
            box.setWindowTitle(tr("Annotations could not be saved"));
            box.setText(tr("%1 annotation(s) could not be saved and will be lost if you continue.")
                            .arg(annotationCount));
            box.setInformativeText(tr("%1\n\n%2")
                                       .arg(QString::fromStdString(failure.message),
                                            QString::fromStdString(failure.path)));
            QPushButton* retry = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
            QPushButton* chooseFolder = box.addButton(tr("Choose Another Folder..."),
                                                      QMessageBox::ActionRole);
            box.addButton(tr("Discard and Close"), QMessageBox::DestructiveRole);
            box.setDefaultButton(retry);
            box.exec();

            if (box.clickedButton() == chooseFolder) {
                // Preferences replaces the repository, so the retry writes to
                // wherever the user just pointed us.
                m_impl->openPreferences();
                return Choice::Retry;
            }
            if (box.clickedButton() == retry) {
                return Choice::Retry;
            }
            return Choice::Discard;
        });
```

This dialog fires on a slide *switch* as well as an explicit close, because `openScene` calls `closeSlide()` internally. That is the point: it is the one place where a failed write would otherwise cost the user everything they had drawn, with no indication it had happened.

- [ ] **Step 4: Report load and save failures**

```cpp
    QObject::connect(annotationPersistence.get(),
                     &app::AnnotationPersistenceService::loadFailed, owner,
                     [this](const QString& path, const QString& message) {
                         QMessageBox::warning(
                             m_impl->owner, tr("Annotations could not be opened"),
                             tr("%1\n\n%2\n\nThe file has not been changed. Annotations are "
                                "disabled for this slide so it cannot be overwritten.")
                                 .arg(message, path));
                     });

    QObject::connect(annotationPersistence.get(),
                     &app::AnnotationPersistenceService::saveFailed, owner,
                     [this](const QString& path, const QString& message) {
                         QMessageBox::warning(
                             m_impl->owner, tr("Annotations could not be saved"),
                             tr("%1\n\n%2\n\nYour annotations are still open and will be "
                                "retried. Choose another folder in Preferences if this "
                                "keeps happening.")
                                 .arg(message, path));
                     });
```

- [ ] **Step 5: Drive the tool's enabled state from the gate**

Replace the unconditional `rectangleToolAction->setEnabled(true)` in the `slideOpened` handler (`MainWindow.cpp:476`) with nothing — the enable now comes from the signal:

```cpp
    QObject::connect(viewportWidget, &ViewportWidget::annotationsAvailableChanged, owner,
                     [this](bool available, const QString& reason) {
                         m_impl->rectangleToolAction->setEnabled(available);
                         if (!available) {
                             // Reset to Pan: leaving Rectangle checked while the
                             // action is disabled leaves a tool the user cannot
                             // turn off.
                             m_impl->viewportWidget->setActiveTool(AnnotationTool::Pan);
                             if (!reason.isEmpty()) {
                                 m_impl->statusBarManager->showTransientMessage(reason);
                             }
                         }
                     });
```

`StatusBarManager::showTransientMessage(const QString&)` (`StatusBarManager.h:30`) takes no timeout — it owns its own. Leave the `slideClosed` handler's `setEnabled(false)` exactly as it is; use the `Impl`'s existing member name for the status bar manager.

- [ ] **Step 6: Add the two menu actions**

In the File menu, after the existing Close action:

```cpp
    openAnnotationsFolderAction = new QAction(tr("Open &Annotations Folder"), owner);
    openAnnotationsFolderAction->setStatusTip(
        tr("Show the folder where annotation files are saved"));
    QObject::connect(openAnnotationsFolderAction, &QAction::triggered, owner, [this]() {
        const QString workspace = m_impl->annotationSettings->workspaceDirectory();
        const QString annotations = workspace + QStringLiteral("/annotations");
        const QString target = QFileInfo::exists(annotations) ? annotations : workspace;
        if (!QFileInfo::exists(target)) {
            QMessageBox::information(
                m_impl->owner, tr("No annotations yet"),
                tr("The annotation folder is created the first time you save an "
                   "annotation. It will be:\n\n%1").arg(annotations));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    });
    fileMenu->addAction(openAnnotationsFolderAction);
```

And a Preferences action in the appropriate menu (Edit, or Tools if there is no Edit menu):

```cpp
    preferencesAction = new QAction(tr("&Preferences..."), owner);
    preferencesAction->setMenuRole(QAction::PreferencesRole);
    QObject::connect(preferencesAction, &QAction::triggered, owner, [this]() {
        openPreferences();
    });
```

- [ ] **Step 7: Apply preference changes**

Add a private `MainWindow::Impl::openPreferences()`:

```cpp
bool MainWindow::Impl::openPreferences()
{
    PreferencesDialog dialog(annotationSettings->workspaceDirectory(),
                             annotationSettings->userName(), owner);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    annotationSettings->setUserName(dialog.userName());
    viewportWidget->annotationModel()->setDefaultAuthor(
        annotationSettings->userName().toStdString());

    const QString chosen = dialog.workspaceDirectory();
    if (chosen != QString::fromStdString(annotationRepository->workspaceRoot())) {
        // The repository is rooted at construction, so a new root means a new
        // repository. Flush to the OLD one before replacing it, or the open
        // slide's unsaved work is written nowhere.
        annotationPersistence->endSlide();
        viewportWidget->setPersistenceService(nullptr);
        annotationPersistence.reset();

        annotationSettings->setWorkspaceDirectory(chosen);
        rebuildAnnotationPersistence();

        if (viewportWidget->isSlideOpen()) {
            // Reopening is how the slide re-enters beginSlide() against the new
            // repository; the ordering in Task 9 makes this safe.
            viewportWidget->reopenCurrentScene();
        }
    }
    return annotationSettings->hasUserName();
}
```

Note the ordering: `endSlide()` runs while `annotationRepository` still points
at the old workspace, and `setWorkspaceDirectory` is called only after that
flush has completed. Setting the preference first would send the flush to a
repository rooted somewhere the user has not finished choosing.

- [ ] **Step 8: Gate annotation creation on a configured name**

In the `rectangleToolAction` triggered handler (`MainWindow.cpp:415`), before switching the tool:

```cpp
        if (!m_impl->annotationSettings->hasUserName()) {
            // FR-USER-01: creation is blocked until an identity is configured.
            // Asked here, on the first attempt to annotate, rather than at
            // startup -- someone who opened the application to look at a slide
            // should not be met by a dialog.
            QMessageBox::information(
                m_impl->owner, tr("Your name is needed"),
                tr("Annotations record who created them. Enter your name in "
                   "Preferences before drawing."));
            if (!m_impl->openPreferences()) {
                m_impl->panToolAction->setChecked(true);
                return;
            }
        }
```

- [ ] **Step 9: Build, install and verify by hand**

```bash
cmake --build build/build --config Release
cmake --install build/build --config Release
```

Run `build/install/release/bin/slideio-viewer.exe` and check:

1. With no name configured, pressing `R` offers Preferences; cancelling leaves Pan selected and the Rectangle tool unused.
2. After entering a name, drawing a rectangle and reopening the slide, the file's `author` field holds that name.
3. **File ▸ Open Annotations Folder** opens the workspace in Explorer; before any save it explains where the folder will be instead of failing.
4. Changing the workspace in Preferences leaves the old files alone and writes new ones to the new location.
5. Hand-edit a saved file to break its JSON, reopen the slide: a dialog names the file and the problem, the Rectangle tool is disabled, and the broken file is **unchanged** on disk afterwards.
6. Hand-edit a saved file's `slideId` to a different value, reopen the slide: the three-way mismatch dialog appears.
7. **The close-failure path.** With a slide open and an unsaved rectangle drawn, make the workspace unwritable — rename `<Documents>/SlideIO Viewer` while the application is running — then open a *different* slide. The three-way "could not be saved" dialog appears naming the count; **Choose Another Folder** followed by a writable path saves and the switch completes. This is the slide-switch path, not an explicit close, and it is the one place a silent failure costs everything the user drew.

- [ ] **Step 10: Run the full suite**

```bash
ctest --test-dir build/build -C Release --output-on-failure
```

Expected: all four suites pass.

- [ ] **Step 11: Commit**

```bash
git add src/ui/src/MainWindow.cpp
git commit -m "Wire annotation persistence into MainWindow with its dialogs and identity gate"
```

---

## Notes for whoever executes this

**The ordering in Task 9 Step 2 is the point of the whole plan.** `endSlide()` must run before `resetAnnotationState()`, not after and not in a `slideClosed` handler. A signal handler fires after the model is already empty, which saves an empty document over the user's work. If a reviewer proposes moving the flush to a signal connection, that is the failure this design exists to prevent.

**`AnnotationModel` ownership deliberately stays with `ViewportWidget`.** Follow-up item 8 proposes moving it to `MainWindow`. That belongs in sub-project F, where annotation panels become a second consumer and give the move something to be validated against.

**Three spec sections have no task because they describe absences**, not work: §10 (deliberate absences), §11 (requirements defects in `documents/`, to be fixed in a separate documentation pass), and §1.1's ruling that nothing clamps to the slide extent.
