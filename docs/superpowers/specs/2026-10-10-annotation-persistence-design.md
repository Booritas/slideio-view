# Annotation persistence (sub-project E1) — design

**Date:** 2026-10-10
**Status:** Approved for planning
**Builds on:** `2026-10-08-annotation-foundation-design.md`, `2026-10-10-annotation-foundation-followups.md`
**Requirements:** FR-ANN-27, FR-ANN-29, FR-ANN-30, FR-ANN-12, FR-USER-01, NFR-MAINT-02
**Architecture:** `documents/03-software-architecture-and-design.md` §4, §9, §10.4, §13

---

## 1. Purpose and scope

The annotation foundation stores annotations in memory only. Close a slide,
change a colour profile, or quit, and the work is gone. This sub-project gives
annotations a file on disk, loads them back when the slide reopens, and does
both without ever writing one slide's annotations against another's
coordinates.

Sub-project E as originally scoped was too large for one plan. It is split:

| | Contents | Status |
|---|---|---|
| **E1** | Save, load, workspace, slide-ID verification, schema version, autosave, author | This spec |
| **E2** | Advisory file locking, multi-process warning (FR-ANN-32) | Deferred |
| **E3** | Write-ahead log, incremental save (architecture §9.4) | Deferred — premature at the hundreds-of-annotations scale the model is built for |

Also out of scope and named here so nobody looks for them: import/export
(sub-project G), encryption at rest (NFR-SEC-04, a "Should"), and crash
session recovery (NFR-REL-04, which needs E3's WAL).

### 1.1 Follow-ups this design closes

From `2026-10-10-annotation-foundation-followups.md`:

- **Item 4** (an ICC profile change wipes every annotation) — closed by §6.
- **Item 5** (annotations carry no author) — closed by §8.
- **Item 6** (rectangles are not clamped to the slide extent) — closed by
  ruling: **no clamping, here or anywhere**. Slide-space coordinates outside
  the extent are valid data, and an annotation clamped on write cannot be
  un-clamped on read. If an off-slide drag is to be prevented, that belongs in
  the drawing tool, not the serializer. This leaves item 9 open, since it was
  only closed conditionally on clamping.

---

## 2. Decisions taken before the design

Recorded because each was a fork with a defensible other side.

**Annotations are stored in a workspace directory, not beside the slide and
not in application data.** Slides frequently live on read-only institutional
storage, so writing beside them fails in the common case. Application data
directories are opaque — the user cannot find, back up, or copy their work.
A workspace is a normal browsable folder.

This matches the architecture doc, which already says the case file lives
"alongside the slide files or in a user-configured workspace directory" (§4),
and it matches `IAnnotationRepository` (§10.4), whose `load(slideId)` takes no
path and therefore already presumes a repository that knows its own root.

It also matches QuPath, which keeps annotations in a project directory keyed
by an internal entry ID and refers to images by URI rather than writing next
to them. QuPath binds entries to images by URI, which breaks when a slide
moves; our content-derived `computeSlideId` does not.

**The native format is our own JSON; GeoJSON is an export format in
sub-project G.** FR-ANN-27 says "JSON format (GeoJSON-based)". Taken
literally, that makes the stored record of what a clinician drew a GeoJSON
`FeatureCollection` — which has no ellipse, so sub-project C's ellipses would
be stored as polygon approximations, or as an approximation plus exact
parameters in a foreign member that can disagree with it. A native format
must not be lossy, and under EU MDR and the EU AI Act's traceability
obligations a stored artifact that approximates the clinician's input is a
liability. GeoJSON remains the right *export*, where lossiness is a
deliberate, documented trade for interoperability. See §11.

**One file per (slide, scene), not per slide.** See §3.2.

---

## 3. File format

### 3.1 Schema version 1

```json
{
  "schemaVersion": 1,
  "slideId": "a3f8c2e1b4d50697",
  "sceneIndex": 0,
  "slide": {
    "fileName": "case001_HE.svs",
    "path": "D:/data/pathology/case001_HE.svs",
    "width": 102400,
    "height": 76800
  },
  "createdAt": "2026-10-10T14:30:00Z",
  "modifiedAt": "2026-10-10T15:02:11Z",
  "annotations": [
    {
      "id": "4f1c8e2a-91b7-4d3e-8c15-2a9f7e6b0d44",
      "type": "rectangle",
      "geometry": {
        "type": "rectangle",
        "topLeft": { "x": 1200.0, "y": 800.0 },
        "bottomRight": { "x": 4400.0, "y": 2600.0 }
      },
      "properties": {
        "label": "",
        "classification": "",
        "color": "#E67E22FF",
        "lineWidth": 2.0,
        "fillOpacity": 0.3,
        "notes": ""
      },
      "metadata": {
        "author": "s.melnikov",
        "createdAt": "2026-10-10T14:31:02Z",
        "modifiedAt": "2026-10-10T14:31:02Z"
      }
    }
  ]
}
```

**Coordinates** are slide pixels of the named scene at pyramid level 0, with
`y` increasing downward — the same space `core::PointF` carries at runtime.
They are written as JSON numbers at full `double` precision.

**Colours** are `#RRGGBBAA`, matching `core::Color`'s four `uint8_t` fields.

**Timestamps** are ISO 8601 UTC with a `Z` suffix.

**`lineWidth`** is screen pixels, not slide pixels, as `AnnotationProperties`
documents. This is deliberate and must survive into the file: a line width in
slide units would render as a hairline at low zoom and a slab at high zoom.

### 3.2 The key is (slide ID, scene index)

`core::computeSlideId` is **file-level by construction**. Its own
documentation is explicit: "identical whichever scene of a multi-scene file is
open — every input is file-level." That was correct for the ICC override
store, because a colour profile belongs to the file.

Annotations do not. They live in the coordinate space of one scene, and scenes
within a file have different dimensions. Keyed on the slide ID alone, a
multi-scene file's scene 1 would load scene 0's annotations and paint them at
scene 0's coordinates — misassociation of exactly the kind FR-ANN-29 exists to
prevent, arriving through a door FR-ANN-29 does not cover.

So the file name carries both:

```
<slideId>.s<sceneIndex>.annotations.json
```

The alternative — one file per slide holding a `scenes` map — was rejected.
It reads better, but every save would become a read-modify-write that has to
preserve the scenes it did not load, and that path's failure mode is the
silent loss of another scene's work.

### 3.3 Fields deliberately absent

Architecture §9.2's schema is ahead of the code in three places. Version 1
omits all three rather than emitting nulls:

- `layers` — no layer concept exists (sub-project D).
- `confidence` — no AI-assisted annotation exists (Stage 5).
- `calibrationSource` — no measurement calibration exists (sub-project H).

Each arrives with its sub-project, as a schema version bump. Absence is
cheaper to add to than a field full of nulls is to give meaning to.

### 3.4 Version handling

`schemaVersion` is an integer, currently `1`.

- Equal to current: load.
- **Greater than current: refuse.** The file was written by a newer version of
  the viewer and may carry annotations this build cannot represent. Loading
  what we understand and saving it back would silently drop the rest.
- Less than current: migrate on load and re-save. No such version exists yet;
  the branch is specified so the first migration has somewhere to go.

Unknown fields within a known version are **dropped, not preserved**. The
alternative — round-tripping unrecognised JSON — gives a false promise of
forward compatibility that the version check already handles honestly.

---

## 4. Storage layout and the workspace

```
<workspace>/                                 default: <Documents>/SlideIO Viewer
  annotations/
    a3f8c2e1b4d50697.s0.annotations.json
    a3f8c2e1b4d50697.s1.annotations.json
    7b20f19c3e8a4411.s0.annotations.json
```

`ui::AppPaths` gains `annotationWorkspaceDirectory()` beside the existing
`logDirectory()`. Resolution: the `QSettings` value when set, otherwise
`QStandardPaths::DocumentsLocation` plus the application name.

Logs stay in `AppLocalDataLocation`. They are diagnostics; this is the user's
work, and the two do not belong together.

The directory is created **lazily, on first save**, not at startup. An
installation that never annotates leaves nothing behind in the user's
Documents.

### 4.1 Making it visible

The transparency requirement is not met by choosing a good default. It is met
by the user being able to get to the files:

- **File ▸ Open Annotations Folder** — `QDesktopServices::openUrl` on the
  workspace. Disabled until the directory exists.
- **A new `PreferencesDialog`** with exactly two fields: the workspace path
  with a browse button, and the user name from §8. No preferences dialog
  exists in the codebase today; this is the minimum one, built to be extended.

Changing the workspace path does **not** move existing files. The dialog says
so. Moving a user's data as a side effect of changing a setting is worse than
leaving it where they put it.

### 4.2 A broken workspace is never silently replaced

If the configured workspace is missing or not writable at save time, the
application reports it, naming the path, and holds the save. It does **not**
fall back to the default directory. A silent fallback puts the user's work
somewhere they have no reason to look.

---

## 5. Component design

Layer placement follows the existing rule: core is Qt-free, infra implements
core interfaces, app orchestrates, ui wires.

### 5.1 core — document and serialization

`nlohmann_json/3.11.3` is **already** a Conan requirement; it has simply never
been linked by any target. It is linked `PRIVATE` to `slideio-viewer-core`, so
it stays out of core's public headers and core acquires no Qt.

```cpp
// AnnotationDocument.h
inline constexpr int kCurrentSchemaVersion = 1;

struct SlideProvenance
{
    std::string fileName;
    std::string path;
    int64_t width = 0;
    int64_t height = 0;
};

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
```

```cpp
// AnnotationSerialization.h
std::string serializeAnnotationDocument(const AnnotationDocument& document);

enum class ParseError
{
    None,
    MalformedJson,
    NotAnAnnotationDocument,
    UnsupportedFutureVersion,
    InvalidField,
};

struct ParseResult
{
    ParseError error = ParseError::None;
    std::string message;              ///< Names the offending field or offset.
    AnnotationDocument document;      ///< Valid only when error == None.
};

ParseResult parseAnnotationDocument(std::string_view json);
```

**No exceptions cross this boundary.** nlohmann's throwing parse is caught
inside. This matches `ColorProfileOverrideStore`, which returns
`std::optional` rather than throwing, and it keeps every failure a value the
caller must look at.

**Non-finite coordinates are rejected on serialize.** JSON cannot represent
NaN or infinity; nlohmann emits `null` for them, producing a file that will
not parse back. `serializeAnnotationDocument` returns an empty string and the
repository treats that as `SaveStatus::Failed` rather than writing a file that
cannot be read. The foundation spec ruled NaN unreachable *through the UI*;
that ruling does not extend to a file format, which must handle what it is
handed.

**Timestamps.** C++17 has no `std::chrono::parse`, so core gains pure helpers
`formatIso8601Utc` and `parseIso8601Utc`. `timegm` is `_mkgmtime` on MSVC; the
implementation handles both.

### 5.2 core — the repository interface

Architecture §10.4's `IAnnotationRepository`, corrected in two ways: the key
carries the scene index (§3.2), and operations return results instead of
throwing.

```cpp
struct AnnotationKey
{
    std::string slideId;
    int sceneIndex = 0;
};

enum class LoadStatus { Loaded, NotFound, Unreadable, Malformed, UnsupportedVersion };

struct LoadResult
{
    LoadStatus status = LoadStatus::NotFound;
    std::string message;
    std::string path;
    AnnotationDocument document;
};

enum class SaveStatus { Saved, NotWritable, Failed };

struct SaveResult
{
    SaveStatus status = SaveStatus::Failed;
    std::string message;
    std::string path;
};

class IAnnotationRepository
{
public:
    virtual ~IAnnotationRepository() = default;
    virtual LoadResult load(const AnnotationKey& key) = 0;
    virtual SaveResult save(const AnnotationDocument& document) = 0;
    [[nodiscard]] virtual std::string pathFor(const AnnotationKey& key) const = 0;
};
```

`pathFor` exists so every error message can name a real path without the app
layer reconstructing the naming rule.

**Deleting the last annotation writes an empty document; it does not delete
the file.** A zero-annotation file records "annotated, then cleared"; a missing
file records "never annotated". There is no deletion path in the repository at
all, which is also one fewer way for data to disappear.

### 5.3 app — the persistence service

```cpp
class AnnotationPersistenceService : public QObject
{
    Q_OBJECT
public:
    AnnotationPersistenceService(core::IAnnotationRepository& repository,
                                 AnnotationModel& model,
                                 QObject* parent = nullptr);

    /// Loads this key's annotations into the model and starts tracking changes.
    void beginSlide(const core::AnnotationKey& key, const core::SlideProvenance& provenance);

    /// Writes now, synchronously, if dirty. No-op when clean or inactive.
    core::SaveResult flush();

    /// flush(), then stop tracking. Safe to call with no slide.
    core::SaveResult endSlide();

    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool isActive() const;    ///< False when no key, or when loading failed.

signals:
    void loadFailed(const QString& path, const QString& message);
    void saveFailed(const QString& path, const QString& message);
};
```

**Dirty tracking lives here, not in `AnnotationModel`.** The service connects
`annotationAdded`, `annotationRemoved` and `annotationChanged` and sets its own
flag, leaving the model ignorant of persistence. `selectionChanged` is
deliberately **not** connected: selection is not persisted, and clicking an
annotation must not dirty the document or trigger a save.

**Autosave** is a 2-second debounce restarted on each mutation, with a
30-second repeating backstop, per architecture §9. Both call `flush()`.

### 5.4 app — `AnnotationModel` additions

```cpp
/// Inserts with the id the annotation already carries. Loading only --
/// add() remains the creation path and keeps minting fresh ids.
void insert(core::Annotation annotation);

/// Replaces the entire contents and clears the selection, emitting one
/// modelReset() rather than N annotationAdded().
void replaceAll(std::vector<core::Annotation> annotations);

signals:
    void modelReset();
```

`add()` is currently the only insertion path and always mints a fresh `QUuid`.
Loading through it would churn every id on every save/load round trip,
breaking anything that later references an annotation — bookmarks, undo, a
panel's selection — across a reopen.

### 5.5 infra — the JSON repository

`JsonAnnotationRepository` implements `IAnnotationRepository` over a workspace
root supplied at construction. Writes go through `QSaveFile`, which is already
write-to-temp-then-atomic-rename, so an interrupted save leaves the previous
file intact.

### 5.6 ui — wiring

- `MainWindow` constructs the repository and the service, and injects the
  service into `ViewportWidget` as a non-owning pointer.
- `ViewportWidget` calls it at the two lifecycle points in §6. It does not own
  it and does not decide policy.
- `AnnotationModel` ownership stays with `ViewportWidget`. Follow-up item 8
  proposes moving it up; that belongs in sub-project F, where panels become
  the second consumer that justifies the move.

---

## 6. Lifecycle and ordering

**On open** — in `ViewportWidget::installSceneOpenResult`, after the stale
`opId` discard, after `resetAnnotationState()` (`ViewportWidget.cpp:2365`), and
after `currentSlideId` is assigned (`:2395`):

```
persistence->beginSlide({currentSlideId, currentSceneIndex}, provenance);
```

**On close** — in `ViewportWidget::closeSlide()` (`:2248`), as the **first**
statement, before the existing body reaches `resetAnnotationState()` (`:2259`):

```
persistence->endSlide();
```

That ordering is the whole answer to follow-up item 4. `reopenCurrentScene()`
(`:2575`) reaches `closeSlide()` through `openScene`, so the flush happens
before the reset; the reopen recomputes the same `slideId` and `sceneIndex`;
`beginSlide` reads the file back. An ICC profile change becomes
indistinguishable from closing and reopening the slide — and exercises the
round trip every time, which is a better test than any we will write.

`resetAnnotationState()` remains the single clearing site. Nothing about this
design adds a second one.

### 6.1 Two gates where persistence must stay off

**Empty slide ID.** `:2239` and `:2345` both leave `currentSlideId` empty when
a slide's scenes cannot be enumerated. `SlideId.h` is explicit that a caller
which cannot enumerate "must decline to look anything up rather than trust the
ID that results — every unreadable file produces it." So with an empty ID: no
load, no save, **and the annotation tools disabled with a visible reason**.
Leaving the tools enabled would let a user draw for an hour and lose all of it
silently, which is worse than not being able to draw.

**Associated images.** `openAuxImage` sets `currentSceneIndex = 0` (`:2309`)
and computes the same file-level slide ID (`:2345`). An annotation drawn on a
label or macro image would therefore be written into scene 0's file and, on
the next open of the tissue, painted over it at the label image's coordinates.
`SceneOpenResult::isAuxImage` already exists (`:57`, `:1235`), so **the
annotation tools are disabled on associated images**, reusing the disable
mechanism built in the foundation's Task 8. This is narrower than widening the
key to distinguish aux images, and annotating a photograph of a slide label
has no clinical use.

---

## 7. Failure handling

| Condition | Behaviour |
|---|---|
| No file for this key | Empty model, tools enabled. Not an error; no dialog. |
| Malformed JSON, or a field of the wrong type | Model empty, tools disabled, dialog naming the path and the parse message. The file is **never overwritten**. |
| `schemaVersion` greater than current | As above, worded as "written by a newer version of SlideIO Viewer". Never downgrade-and-save. |
| File unreadable (permissions, I/O error) | As above, naming the OS error. |
| Embedded `slideId` differs from computed | Warn, with three choices: **Open read-only** (annotations load and are drawn, tools disabled, no save ever attempted), **Re-associate** (rewrite the embedded ID to this slide and enable saves), **Ignore** (empty model, tools disabled). |
| Workspace missing or read-only at save | Dialog naming the path; save held; model untouched; **no fallback to the default folder**. Retried on the next autosave tick. |
| Save fails during a close | The close does not proceed silently: **Retry**, **Choose another folder**, or **Discard and close**, the last naming how many annotations will be lost. |

**The governing rule: a file we could not parse is never a file we write to.**
`AnnotationPersistenceService` enforces it — not the repository, which is
stateless and cannot know that a previous load failed. A failed or
read-only-resolved load leaves `isActive()` false, and `flush()` on an
inactive service is a no-op. The single worst outcome available to this
subsystem is overwriting a user's only copy of their work with an empty
document because we could not read it, and this is what prevents it.

The last row carries more weight than its size suggests. `openScene` calls
`closeSlide()` internally, so a failed flush during a *slide switch* is
precisely where work would otherwise disappear with no dialog in sight.

A mismatch is reachable even though the file name is derived from the slide
ID: the file may have been copied or renamed by hand, restored from a backup
of a different machine, or written for a slide that has since been re-exported
by the scanner. The embedded ID is what FR-ANN-29 actually asks us to verify;
the file name is only where we looked.

Re-association deserves a note. Architecture §9 says a matching slide ID "must
never be treated as evidence that an annotation file actually belongs to the
slide it is opened against". The converse holds too: a mismatch is not proof of
error — a slide re-exported by a scanner has a new content hash and the same
tissue. So re-association is offered, never performed automatically, and the
dialog shows both the stored `slide.fileName` and the current one so the user
has something to judge by. That provenance block is the reason it is in the
file at all.

---

## 8. Identity and author

Architecture §13's Tier 1 is implementable now and needs nothing from Stage 4:
a username configured in preferences, stored in `QSettings`, from which the
annotation `author` field is derived and which is not editable per annotation.

- The field lives in the new `PreferencesDialog` (§4.1).
- With no name configured, the prompt appears on the **first annotation
  attempt**, not at startup. FR-USER-01 blocks annotation creation, not
  viewing; a pathologist who opened the application to look at a slide should
  not be met by a dialog.
- `author` is stamped at creation and **never rewritten on load**. An
  annotation made by a colleague keeps their name when the file is opened here.
- The name is **self-asserted and unverified** — the same standing as the slide
  ID. Nothing authenticates it, and no downstream consumer may treat it as
  attribution evidence.

An author name is personal data, and it travels inside a file users will send
to colleagues. Under GDPR that makes its handling a design concern rather than
an afterthought: this design puts it in exactly one place in the schema so that
NFR-SEC-03's "export for sharing" redaction, in sub-project G, has a single
field to strip rather than a search to perform.

---

## 9. Testing

No test suite creates a `QApplication` or a `QCoreApplication`, so no widget
can be constructed and no `QTimer` will ever fire. The autosave decision is
therefore extracted as a pure function, following the foundation spec's §4
mandate that failure-prone decisions become pure testable functions — the same
pattern as `ColorProfilePolicy`, `AnnotationInteraction` and `DriverFilters`:

```cpp
bool shouldAutosave(std::chrono::steady_clock::time_point lastMutation,
                    std::chrono::steady_clock::time_point now,
                    bool dirty,
                    std::chrono::milliseconds debounce,
                    std::chrono::milliseconds maxInterval);
```

The `QTimer` becomes a dumb clock that asks it.

**core-tests** — the whole of serialization, which is where the data-loss bugs
live:

- Round-trip: document to JSON to document, every field equal.
- Every `ParseError`: truncated JSON, a JSON array at the root, a missing
  `annotations` key, a string where a number belongs, an unknown geometry
  `type`.
- `schemaVersion` of 2 rejected as `UnsupportedFutureVersion`.
- Coordinate precision: a `double` that needs 17 significant digits survives.
- Non-finite coordinates rejected on serialize rather than written as `null`.
- Timestamp format and parse, including a value before 1970 and one with a
  two-digit month and day.
- Colour round-trip including a non-opaque alpha.
- `shouldAutosave` at each boundary: clean, within debounce, past debounce,
  past `maxInterval` while still inside the debounce window.

**app-tests** — the service against a fake repository: a mutation dirties, a
selection change does not, `flush()` clears the flag, loading does not dirty, a
failed load leaves `isActive()` false and makes `flush()` a no-op, `endSlide()`
with no slide is safe.

**infra-tests** — `JsonAnnotationRepository` against a temporary directory:
path construction for scene 0 and scene 3, a save followed by a load, no `.tmp`
file left behind, a read-only directory reported as `NotWritable`, an absent
file reported as `NotFound` rather than an error.

**No widget tests.** The two `ViewportWidget` hooks are one line each; they are
covered by the final whole-branch review and by hand in the running
application, including the ICC round trip that item 4 is about.

---

## 10. Deliberate absences

Named so that their absence reads as a decision rather than an oversight.

- **No file locking.** Two instances editing one slide's annotations will have
  a last-writer-wins collision. That is E2 (FR-ANN-32).
- **No write-ahead log.** Every save writes the whole document. At the
  hundreds-of-annotations scale `AnnotationModel` is built for — its own header
  says a flat vector searched linearly is adequate — the O(N) rewrite is
  cheaper than the complexity of E3.
- **No crash recovery.** Up to the autosave interval of work is lost on a hard
  kill. NFR-REL-04 wants better; it needs E3.
- **No clamping to the slide extent.** See §1.1.
- **No import or export.** Sub-project G.
- **No encryption.** NFR-SEC-04 is a "Should", and an institutional keystore is
  a project of its own.
- **No migration code.** Only version 1 exists. The branch is specified in §3.4
  so the first migration has a defined home.

---

## 11. Requirements defects

Carried forward from the foundation's list, plus three found here. These are
defects in `documents/`, not in the code, and are worth fixing at source before
an implementer reads those sections.

1. **FR-ANN-27 is assigned to two different requirements** — "jump to
   next/previous annotation" in SRS §5.4, and "annotations shall be stored in
   JSON format" in §5.5. A duplicated requirement ID undermines traceability.
   *(Carried from the foundation spec; still open.)*

2. **Architecture §10.4 puts `QSize` in `ISlideSource`**, inside the layer
   required to be Qt-free. The shipped header correctly does not. A
   documentation defect, sitting in the section whoever implements persistence
   reads first. *(Carried from the foundation spec; still open.)*

3. **FR-ANN-27's "(GeoJSON-based)" should be amended** to "JSON, with GeoJSON
   as an export format", per §2. As written it mandates a lossy native format
   for every shape GeoJSON cannot express.

4. **FR-ANN-29 assumes the slide identifier alone prevents misassociation.** It
   does not, for multi-scene files — see §3.2. The requirement should say the
   annotation file is keyed by slide identifier **and scene index**.

5. **Architecture §10.4's `IAnnotationRepository` throws on failure** and takes
   only a `slideId`. Both are corrected in §5.2; the document should follow.
