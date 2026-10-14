# Annotation panels: list and properties

**Sub-project F of Stage 3.** Written 2026-10-14 against `main` @ `9826da6`.

Adds two docking panels — an annotation list and a properties editor — and, in
doing so, moves `AnnotationModel` out of the presentation layer. F is the first
point where something other than `ViewportWidget` reads and mutates the model,
which is what makes the move necessary rather than tidy.

## Scope

**In:** the annotation list panel, the annotation properties panel, the
ownership move (follow-up item 8), an app-layer seam for undoable mutation, and
a navigate-to-annotation call on the viewport.

**Out, each its own sub-project:**

- **Layers.** `core::Annotation` has no layer field. Layers would change core,
  the serializer and its `schemaVersion` policy, the renderer and hit-testing.
  That is a data-model change wearing a panel's clothes, and it does not belong
  in the same spec as two read-mostly views.
- **Multi-select (FR-ANN-16).** `AnnotationModel` carries a single
  `m_selectedId`, and every invariant around selection — `setSelected` refusing
  an unknown id, the empty id clearing, the viewport painting exactly one
  handle set — is written for one. F keeps single-select.
- **Measurements (area, perimeter).** They need the scene's microns-per-pixel
  and a unit policy. The properties panel has an obvious place to show them
  later; it does not show them now.
- **A classification taxonomy UI.** F offers five fixed suggestions and accepts
  free text. `classification` stays a plain string, so the serializer and
  `schemaVersion` are untouched.

## 1. Ownership and wiring

`MainWindow::Impl` gains `std::unique_ptr<app::AnnotationModel> annotations`,
created immediately before `new ViewportWidget(...)`. `ViewportWidget` takes it
by reference in its constructor:

```cpp
ViewportWidget(app::AnnotationModel& model, QWidget* parent = nullptr);
```

`ViewportWidget::annotationModel()` is **removed**, and the five call sites in
`MainWindow.cpp` read the Impl member directly. Removing the accessor rather
than leaving it as a convenience is the point: it is the hole item 8 came
through, and a panel reaching application state through a presentation widget
is exactly what this sub-project exists to stop.

### Why a constructor argument for the viewport and setters for the panels

The viewport's 24 uses of the model are unconditional, `paintAnnotations` among
them. A null-tolerant model pointer there would mean roughly eight early
returns describing a state that should never exist. `setPersistenceService` and
`setUndoStack` are setters because the viewport genuinely works without those
collaborators — its six null-guards are real logic, not ceremony.

A panel with no model is a legitimate state: it shows nothing. So the panels
take `setAnnotationModel(app::AnnotationModel*)` and tolerate null. The
asymmetry is deliberate.

### Lifetime

`m_impl` is destroyed before `~QMainWindow` deletes the window's children, so
anything Qt-owned that holds the model outlives it. `~MainWindow` today cuts
the viewport's two raw links by setting them to `nullptr`; a reference cannot
be cut, so that block instead deletes the viewport outright before `m_impl`
goes away:

```cpp
delete m_impl->viewportWidget;
m_impl->viewportWidget = nullptr;
```

The panels are deleted in the same block, ahead of the model. This keeps the
property the existing comment is defending: the ordering is a local, visible
fact, not a claim about what some other file's destructor happens not to do.

Their null tolerance is therefore not about teardown — it covers the window
between construction and `setAnnotationModel`, during which a dock that the
restored layout makes visible can be asked to paint itself.

### Unchanged

`buildAnnotationPersistence()` keeps owning the repository, the persistence
service and the undo stack, and keeps its lifecycle wiring; it binds
`*m_impl->annotations` instead of `*viewportWidget->annotationModel()`. The
`cleared`/`modelReset` to `clearUndoUnlessReopening` connections, the
`isPreservingUndoContext()` window, and `closeSlide()`'s flush-before-reset
ordering are all untouched.

The renderer needs no work: `paintAnnotations` already honours
`properties().color`, `lineWidth` and `fillOpacity`, and the six model-signal
connections added by sub-project D already repaint on `annotationChanged`. A
properties edit therefore shows on screen through machinery that exists.

## 2. The mutation path

`ViewportWidget::pushCreateCommand` and `pushDeleteCommand` touch no widget
state. Each finds the annotation, copies it, builds a description and pushes an
`AnnotationEditCommand` over `(model, undoStack)` — app-layer operations living
in a presentation widget, the same inversion as item 8 one level down. Nothing
notices today because the viewport is the only mutator. A panel calling
`model->remove(id)` directly would produce an edit undo cannot reach.

Free functions in `app`:

```cpp
bool deleteWithUndo(AnnotationModel& model, UndoRedoStack& stack,
                    const std::string& id, std::string description);
bool setPropertiesWithUndo(AnnotationModel& model, UndoRedoStack& stack,
                           const std::string& id, core::AnnotationProperties properties,
                           std::string description);
```

Each mutates through the model's own API and then pushes the command describing
before-to-after — the established order, since commands are recorded after the
model has already changed and `execute()` runs only on redo. The description is
a parameter: the user-facing wording (`tr("Delete %1")`, `shapeName()`) is
ui-layer text, so app does the mechanics and ui supplies the words.

`pushDeleteCommand` delegates to `deleteWithUndo`, so there is one
implementation rather than two that can drift.

`pushCreateCommand` stays in the viewport. Creation is inherently a drawing
gesture and has no second caller.

### `shapeName` must be promoted

The user-facing shape names ("Rectangle", "Polygon", "Freehand Line") come from
`shapeName`, which today sits in an anonymous namespace in
`ViewportWidget.cpp`. Three callers need it after F: the viewport's undo
descriptions, `MainWindow` building the list panel's delete description, and
the list panel's Type column. It is also a `std::visit` over the geometry
variant — one of the exhaustive dispatch sites a new shape must update — so
copying it would create a second place to forget.

It moves to a small shared ui header, `AnnotationShapeName.h`, unchanged:

```cpp
QString shapeName(const core::Annotation& annotation);
```

One visit site, three callers, and still the single place a new geometry type
must be named for the user.

### Commit granularity

A command per keystroke would fill the 50-step stack from one label.
`setPropertiesWithUndo` is called on commit, never on change: `editingFinished`
for text fields, `activated` for the classification combo, the colour dialog's
accepted result. The panel compares against current properties first and does
nothing when they are equal, so tabbing through an untouched field pushes no
step and does not restamp `modifiedAt` — the rule the Escape-cancels-a-move fix
established in `9826da6`.

## 3. Annotation list panel

`AnnotationListPanel : QDockWidget`, `objectName` `"AnnotationListPanel"`,
hidden by default, `toggleViewAction()` bound to **Ctrl+Shift+L** and added to
the View menu after the metadata toggle.

Contents: a filter `QLineEdit`, a sort `QComboBox`, and a three-column
`QTreeWidget` (Label, Type, Classification) whose rows carry the annotation id
in `Qt::UserRole`. A widget tree rather than a `QAbstractItemModel`, following
`SlidePropertiesPanel`: at the hundreds-of-annotations scale `AnnotationModel`
already commits to, an item model would mostly be a second place for selection
state to live.

### Rebuild policy

Full rebuild on `annotationAdded`, `annotationRemoved`, `annotationChanged`,
`cleared` and `modelReset`. **Not** on `selectionChanged` — that moves a
highlight, and rebuilding would destroy the item under the user's cursor
mid-click. Full rebuild elsewhere is right at this scale and removes the whole
class of incremental-sync bugs.

### Selection

Two-way. Panel click sets `model->setSelected(id)`. Model `selectionChanged`
highlights the row, applied under a `QSignalBlocker` on the tree. The loop
would terminate anyway because `setSelected` returns early on an unchanged id,
but relying on that makes the panel's correctness a fact about another class.

### The panel never touches the viewport

It emits `annotationActivated(const std::string& id)` on double-click and
`deleteRequested(const std::string& id)` on Delete. `MainWindow` owns both ends
and routes them — activation to viewport navigation, deletion to
`deleteWithUndo` with the ui-built description. A panel that reached for
`ViewportWidget` to zoom would rebuild the dependency section 1 removes.

`ViewportWidget` has no navigate-to-rect API — only `fitToSlide()` — so F adds
one: centre on an annotation's bounding box at a zoom that fits it with margin.

### Testable core — `AnnotationListPolicy.h`

```cpp
enum class AnnotationSortKey { CreationOrder, Label, Classification, Type, ModifiedTime };

struct AnnotationRow { std::string id, label, type, classification; };

/// Display name per geometry type, indexed by core::AnnotationType.
using AnnotationTypeNames = std::function<std::string(core::AnnotationType)>;

std::vector<AnnotationRow> annotationRows(const std::vector<core::Annotation>& annotations,
                                          const std::string& filterText, AnnotationSortKey sort,
                                          const AnnotationTypeNames& typeName);
std::string annotationDockTitle(std::size_t total, std::size_t shown);
```

The type names are passed in rather than looked up inside. `shapeName` returns
a translated `QString`, and this function has to stay Qt-free to be worth
testing — the panel supplies a lambda wrapping `shapeName`, and tests supply
plain ASCII names. That also makes the `"<Type> N"` fallback assertable without
a `QApplication`, which is the whole reason the function exists.

- An unlabeled annotation displays as `"<Type> N"`, N being its 1-based
  position in **creation order**, not row order. Forty untitled traces still
  have to be told apart, and the number must stay attached to the same shape
  when the sort changes.
- Filtering is case-insensitive substring over the **displayed** label and the
  classification **only** — not notes. A notes match would show a row whose
  every visible column contains nothing resembling the search text. The
  displayed label rather than the stored one, because they differ for an
  unlabeled annotation: matching `properties.label` would hide a row whose
  Label column reads exactly what was typed. The Type column stays out of the
  filter, so a labelled annotation is not found by its shape's name.
- Every sort breaks ties by creation order, so identically-labeled rows never
  swap between rebuilds.
- `annotationDockTitle` yields `"Annotations (12)"`, or `"Annotations (3 of
  12)"` while a filter hides some, so a filtered list cannot be mistaken for an
  empty slide.

## 4. Annotation properties panel

`AnnotationPropertiesPanel : QDockWidget`, `objectName`
`"AnnotationPropertiesPanel"`, hidden by default, **Ctrl+Shift+E**
(`Ctrl+Shift+P` belongs to the slide properties dock), View menu.

**Editable:** Label (`QLineEdit`); Classification (editable `QComboBox`
offering Tumor, Stroma, Necrosis, Normal, Artifact, free text accepted, no
auto-colour); Colour (swatch button opening `QColorDialog`); Line width
(`QSpinBox`, 1-5 screen pixels, matching FR-ANN-11); Fill opacity (`QSlider`,
0-100%); Notes (`QPlainTextEdit`).

**Read-only:** Type, Author, Created, Modified. Timestamps display through the
existing `core::Iso8601` — a second time format would be a second thing to get
wrong.

**Empty state is a real state.** No selection means every field cleared and
disabled behind a placeholder line. A disabled-but-populated panel still
showing the last annotation's label reads as if that annotation were selected.

**Repopulation:** on `selectionChanged`, and on `annotationChanged` when the id
matches the selection, so undoing a properties edit moves the fields back. One
exception: **never repopulate the widget that currently holds focus.**
`setPlainText` resets the caret to the start, which would move it out from
under someone typing. A panel's own commit writes identical values back anyway,
since the commit already compared and skipped no-ops.

**Pending edits survive a selection change.** The panel commits any outstanding
field before repopulating, rather than trusting Qt to deliver focus-out ahead
of the new selection. Typing a label and clicking another annotation in the
list must not discard what was typed.

### Testable core — `AnnotationPropertiesEdit.h`

```cpp
struct AnnotationFieldValues {
    std::string label, classification, notes;
    core::Color color;
    float lineWidth, fillOpacity;
};

std::optional<core::AnnotationProperties> propertiesEdit(const core::AnnotationProperties& current,
                                                         const AnnotationFieldValues& fields);
std::string propertiesEditDescription(const core::AnnotationProperties& before,
                                      const core::AnnotationProperties& after);
```

`propertiesEdit` returns `nullopt` when nothing changed — section 2's no-op
rule, made testable; it is what keeps a tab-through from restamping
`modifiedAt` and triggering an autosave. It clamps `lineWidth` to 1-5 and
`fillOpacity` to 0-1, so a value the renderer would misdraw cannot reach the
model.

`propertiesEditDescription` gives the Edit menu something true to say:
"Rename annotation", "Reclassify annotation", "Recolour annotation", or "Edit
annotation" when more than one field moved. The menu renders
`undoDescription()`, so without this every properties edit would read
identically and two of them could not be told apart on the stack.

## 5. Testing

### Automated

`tests/app/AnnotationEditingTest.cpp` — delete pushes exactly one step; undo
restores the annotation **at its original index**, not appended (paint order
and hit-test order both follow that vector, so the position is user-visible); a
properties edit round-trips through undo and redo; an unknown id mutates
nothing and pushes nothing; a no-op properties edit pushes nothing and leaves
`modifiedAt` alone. This is the first automated coverage annotation mutation
has had — the equivalent logic in `ViewportWidget` is unreachable from any
suite. `AnnotationModel` is a `QObject`, but a `QObject` needs no
`QCoreApplication`; `AnnotationModelTest.cpp` already constructs one.

`tests/ui/AnnotationListPolicyTest.cpp` — the `"<Type> N"` fallback numbered in
creation order and staying attached across a sort change; case-insensitive
filtering over the displayed label and classification but not notes; tie-break
stability across all four sort keys;
`annotationDockTitle` in both forms.

`tests/ui/AnnotationPropertiesEditTest.cpp` — `propertiesEdit` returning
`nullopt` for an unchanged field set; clamping; preserving a free-text
classification outside the five offered; `propertiesEditDescription` naming
each single-field case and falling back to the generic wording.

**Build note:** every source and test file is listed explicitly —
`src/app/CMakeLists.txt`, `src/ui/CMakeLists.txt` and `tests/CMakeLists.txt`
all enumerate their `.cpp` files. The only glob is `file(GLOB_RECURSE
_ui_headers ...)` in `src/ui/CMakeLists.txt`, which exists so AUTOMOC finds
`Q_OBJECT` headers under `include/`; it does not pick up `.cpp` files. Every
new source and every new test file must be added by hand, and one that is not
simply does not build — the link error names the missing symbol, not the
cause.

### Manual — group V

The panels and the new viewport navigation are unreachable from any test, so
they get a check list, following the P-group and U-group precedent. Run against
an installed binary, with the viewer killed before `cmake --install` and the
install verified by timestamp, not exit code.

**Setup — the fixture slides carry annotations from the U-group run of
2026-10-14.** The H&E slide holds 5 annotations where earlier specs describe 3,
and `pJP31mCherry.czi` has a file holding 1 that it did not have before. These
were left in place deliberately. V3 therefore checks that the list's count
matches what the slide actually holds and what the viewport actually paints —
it is not written against a fixed number, and a reader finding 5 where an older
document says 3 has found the drift, not a defect.

| # | Check |
|---|-------|
| V1 | Both docks toggle from the View menu and from Ctrl+Shift+L / Ctrl+Shift+E |
| V2 | Dock positions and visibility survive a restart (`saveState`/`restoreState`) |
| V3 | Opening a slide with saved annotations populates the list; title shows the count |
| V4 | Drawing a shape adds its row immediately |
| V5 | Clicking a row selects the shape in the viewport |
| V6 | Clicking a shape in the viewport highlights its row |
| V7 | The filter hides non-matching rows; the title reads "N of M" |
| V8 | Each sort order behaves; the `"<Type> N"` number stays with its shape across a sort change |
| V9 | Double-clicking a row navigates the viewport to that annotation |
| V10 | Delete in the list removes the annotation; Edit menu reads "Undo Delete <shape>"; Ctrl+Z restores it at its original paint order |
| V11 | Editing a label updates the row, reaches the file, and survives a slide reopen |
| V12 | Editing colour, line width and opacity repaints the viewport immediately |
| V13 | Tabbing through fields without changing anything pushes no undo step and leaves `modifiedAt` unchanged in the file |
| V14 | Ctrl+Z inside the notes field and the filter box edits that text, not an annotation |
| V15 | An uncommitted label commits when the selection changes, rather than being discarded |
| V16 | Switching slides clears both panels; no stale rows |

### Results — run 2026-10-14 against `main` @ `054fc7e`

Driven through UI Automation plus raw `keybd_event`, so the readings are exact
widget state rather than pixels. The install was verified twice before starting
— matching timestamps on the build and installed binaries, **and** strings this
branch introduced found inside the installed `.exe` — because a running viewer
turns `cmake --install` into a silent no-op that still exits 0.

**Passed:** V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V14, V15, V16,
V17, V18, V19 (two of its three call sites), V22, V24.

Three results are worth reading in full:

- **V15** — typing a label for one annotation and then clicking a *different*
  row commits the text to the annotation it was typed for, leaving the other
  untouched. This is the scenario an earlier draft of the plan would have
  corrupted, by committing against `selectedId()` after the model had already
  moved on.
- **V22** — on a slide opened read-only after a slide-ID mismatch, the list
  panel's Delete refuses with "These annotations are open read-only and cannot
  be changed", pushes no undo step, and leaves the annotation in place. A
  properties edit is refused the same way and the field snaps back. Before this
  branch's final review, that path deleted the annotation, wrote nothing, and
  said nothing.
- **No spurious edit on selection** — selecting an annotation leaves Undo
  disabled and `modifiedAt` equal to `createdAt`. Line width and fill opacity
  round-trip through the widgets without registering as a change.

**Not run:** V21 needs a slide carrying hundreds of annotations; V20 and V23 were
not reached.

**Open UX question, not a defect:** in read-only mode the properties fields stay
enabled, so a user can type and is then told why the edit was refused. Greying
them out instead is a defensible alternative and should be a deliberate choice.

### Added during implementation

These came out of code review rather than the original design, and each one
exists because a reviewer traced a path no test in this repo can reach.

| # | Check |
|---|-------|
| V17 | **Tested 2026-10-14: passes.** Scroll the list away from the selected row, then change the selection from the viewport — the row scrolls into view. A review flagged `AnnotationListPanel.cpp:171`'s scrollbar restore as overwriting `applySelection`'s `scrollToItem`, but that only applies to rebuild paths, and `selectionChanged` deliberately connects to `applySelection` alone rather than to `rebuild`. Two narrow rebuild cases do keep their prior scroll offset instead of following the selection — reopening the dock, and changing the sort — which is defensible behaviour, though the comment at `:125-127` claims otherwise |
| V18 | The View menu entry still reads "Annotation &List", with Alt+V,L working, after the list has rebuilt many times. Qt's `QDockWidget` overwrites its own toggle action's text whenever the dock title changes, and the dock title carries the annotation count |
| V19 | Type a label, then File→Close (or open another slide, or quit) **without clicking away first**. The typed label must reach the file. A menu click moves no focus widget, so nothing commits the field unless `commitPendingEdit()` runs |
| V20 | Hide the properties dock while a field holds an uncommitted edit; confirm the value survives. This depends on Qt delivering focus-out at hide time, which is not guaranteed |
| V21 | Drag an annotation with both docks open on a slide carrying many annotations. The viewport updates the model per mouse-move, so the panels used to rebuild per frame; they now skip work while hidden, but a visible dock still rebuilds. Watch for lag |
| V22 | On a slide whose annotation file fails to parse (the read-only state), confirm Delete **from the list panel** and a properties edit are both refused with the status-bar message, not applied silently. The viewport's own Delete already refused; the panels are new paths to the same operation |
| V23 | Invoke Undo from the Edit **menu** (not Ctrl+Z) while a properties field holds an uncommitted edit. Whether the edit is saved or discarded depends on whether opening a menu sends focus-out; both outcomes are safe but they look different, so note which you see |
| V24 | On a read-only slide, select an annotation and confirm the properties fields are **disabled** while something is selected — not merely blank. The empty state greys the same six fields, so a reading taken with nothing selected proves nothing |

**Regression surface from section 1.** The ownership move rewrites the viewport
constructor and `~MainWindow`'s teardown, so group V ends by re-running the
existing checks that exercise model lifetime: a slide switch, a colour-profile
reopen (same scene, same ids, undo stack preserved), and a clean shutdown with
both panels open and a dirty autosave pending. All three have documented steps
in the P and U groups.

## Compliance note

Annotations are clinical findings drawn on patient images. Nothing in F changes
what is stored or where — the same per-(slide, scene) JSON files under the
user's workspace, the same `schemaVersion` 1 — so F introduces no new
PHI-handling surface. The `author` field remains self-asserted and
unauthenticated, as `setDefaultAuthor` already documents; that is unchanged by
F and remains a gap for the user-identity work in Stage 4, which is where
EU MDR and EU AI Act record-attribution obligations will have to be met.
