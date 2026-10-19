# Categorized counting — design

Status: implemented in commits `d6dacc7..4980a86`, 2026-10-18. Manual group AC was run in full across 2026-10-18 and 2026-10-19: 28 pass, 1 fail (AC9), 1 performable only in part (AC29) — see §7.5. AC26 failed, was fixed, and re-ran green. The three cosmetic defects the run turned up were fixed on 2026-10-19 and are recorded with their causes in §7.5; AC9 and AC29 remain open because each needs a decision the spec never asked for.

## 1. Scope

### 1.1 What this round is

A pathologist counting objects on a slide picks a category once and then
clicks. Every marker placed carries that category without re-picking, and the
running tally per category is visible while counting.

Four parts, all serving that one workflow:

1. The category list becomes the user's own (FR-ANN-13, an unmet **Must**).
2. A current category is stamped onto point markers as they are created.
3. A counting strip at the top of the Annotation List panel shows the current
   category and the per-category tallies.
4. A category can be renamed, retagging every annotation on the scene that
   carries it, in one undo step.

The fourth part was added during the design discussion, after checking what
recovery from a mistyped category would actually cost. See §6.1: without it the
round ships its most likely user error with no proportionate way out.

### 1.2 What this round is not

The point marker shipped 2026-10-17 was the placement half of this workflow;
this is the bookkeeping half. Nothing here adds a geometry type, and §8 lists
what is deliberately left out.

## 2. The category is the existing classification

### 2.1 One axis, not two

`core::AnnotationProperties::classification` already exists, is already
persisted by `AnnotationSerialization`, and is already surfaced three ways: a
Classification column in the list, an editable combo in the properties panel,
and the list's "Filter by label or classification" box.

A counting category goes in that field. "Mitosis" sits where "Tumor" sits.

The alternative — a second `category` field beside `classification`, so a
marker could be "mitosis" **and** "in stroma" — was considered and rejected.
It would add a persisted property, and an older build would silently drop it
when it rewrote the file: the staggered-rollout problem `schemaVersion` exists
to manage, incurred for an axis nobody has asked for yet. The cost of one axis
is real and is accepted: an annotation carries one label, so a marker counted
as a mitosis cannot also record the tissue context it sat in.

### 2.2 The file format does not change

`classification` is already written and already read. `schemaVersion` stays 1,
`AnnotationSerialization.cpp` is untouched, and a file written by this round
opens in the shipped build with every category intact. This is the one round in
recent memory where the serializer is not a risk.

## 3. The configurable category list

### 3.1 Storage

An application preference, stored in QSettings: the same list on every slide and
every workspace.

This is justified by what the list actually is. Each annotation already carries
its own category **string**, so the saved findings are self-describing without
the list. The list is a picker convenience, not the record — which is why it
does not belong in the annotation file, and why losing it would cost a user
their suggestions and none of their data.

`infra::QSettingsAnnotationCategoryStore` holds it, mirroring
`QSettingsColorProfileOverrideStore`: a default constructor using the
application's own settings, and an explicit INI-path constructor **so
infra-tests never touch the user's real configuration**.

It gets **no core interface**, unlike the profile store. That store has
`core::IColorProfileOverrideStore` because core needs to reach it. Nothing in
core needs the category list — core needs only the pure normalisation function
in §3.2 — and the consumers are ui panels, which already link infra. An
interface here would be inversion with nothing to invert.

### 3.2 Defaults and normalisation

Unset, the store answers FR-ANN-13's defaults: `Tumor`, `Stroma`, `Necrosis`,
`Normal`, `Artifact`. "Unclassified" is **not** among them; see §5.2.

The Preferences editor is free text, so what comes back from it is normalised by
a pure core function:

```cpp
// core/AnnotationCategories.{h,cpp}
[[nodiscard]] std::vector<std::string> normalizeCategoryList(
    const std::vector<std::string>& raw);
```

It trims surrounding whitespace, drops entries that are empty after trimming,
removes case-insensitive duplicates keeping the first spelling, and otherwise
preserves order. Order is preserved rather than sorted because §5.2 displays
tallies in list order, and a list that sorted itself would reorder the counting
rows behind the user's back.

It lives in core rather than ui because it is a rule about the data, it is
Qt-free, and core-tests are the cheapest suite to run.

### 3.3 `kAnnotationClassifications` stops being the source of truth

`ui::kAnnotationClassifications` — the hardcoded five in
`AnnotationPropertiesEdit.h` — becomes the seed for §3.2's defaults and nothing
else. The properties panel's combo must read the configured list from the store,
or two places in the application disagree about the vocabulary: you would add
"Mitosis" in Preferences, see it when counting, and not see it when editing an
annotation's properties.

This is why the round touches `AnnotationPropertiesPanel` whether or not it
wants to. Both panels take the store.

### 3.4 Preferences

A third row below "Annotation workspace" and "Your name". The dialog has two
settings today, so this is a small extension to a small dialog.

The row edits the list, which is where a category is deleted or corrected
outside the counting flow (§6.5 covers correcting it inside the flow).

## 4. The sticky category

### 4.1 It lives in the model

`AnnotationModel::setCurrentCategory(std::string)`, and `add()` stamps it into
the new annotation's properties exactly where `m_defaultAuthor` is already
stamped into its metadata. The precedent is not an analogy; it is the same
mechanism, three lines apart.

It goes in the model rather than in `ViewportWidget` for the reason that governs
every decision in this codebase: **app-tests can construct an `AnnotationModel`,
and nothing can construct a `ViewportWidget`**, because no test binary creates a
`QApplication`. A rule placed in the viewport is a rule no test will ever check.
This is the same move as extracting `annotationPaintMarginScreenPixels` in the
point/arrow round, and it was made there only after a reviewer found the defect.

### 4.2 Markers only, decided by geometry

`add()` stamps the current category only when
`core::typeOf(geometry) == core::AnnotationType::Point`.

The test is on the geometry, not on the active tool, because the model does not
know about tools and should not learn. It also means the rule holds for any path
that creates a point marker, not only the Point tool.

Scoping it to markers is deliberate: a rectangle drawn for an unrelated reason
must not pick up the counting label because the user was counting ten minutes
ago. A shape silently acquiring a clinical classification nobody chose for it is
worse than one acquiring none.

### 4.3 Creation only, never retroactive

Changing the current category changes what the **next** marker gets. It never
touches an annotation that already exists. Retagging what exists is §6, is
explicit, and is undoable.

### 4.4 Lifetime

The current category is session state. It survives a slide switch, because a
study is often several slides counted the same way, and it is **not** persisted
across restarts, because a category that silently survived a restart would stamp
markers on a slide opened days later for an unrelated reason.

The combo offers an explicit **(none)** entry, which clears it. Without that
there is no way back to placing unclassified markers short of restarting.

### 4.5 How a typed category joins the list

The counting combo is editable, as the properties combo already is. Typing a
value that is not in the list sets it as the current category immediately.

**It joins the configured list when the first marker is counted under it, not
when it is typed.** A category earns its place by having something counted under
it, which is exactly what the list is for, and a typo noticed before the first
click leaves nothing behind. A typo not noticed appears as its own tally row,
directly under the user's eye, which is where §6 fixes it.

Undoing that first marker does not remove the category from the list. The list
is a vocabulary, not a derived view of the current data; un-saying a word is
§3.4's job or §6.5's.

## 5. The counting strip

### 5.1 Where and what

A compact strip at the top of the existing Annotation List dock, above the
filter box: the current-category combo, then one row per category with its
count. The dock is open by default and the strip sits directly above the list
whose rows it is counting.

The accepted cost is that the Annotation List panel grows a second
responsibility and the strip competes for height with the list itself.

On a slide whose annotations are not editable the current-category combo is
disabled, through the same gate that already disables the drawing tools and the
properties panel's fields: `MainWindow::applyAnnotationAvailability` calls
`setEditingAllowed(available)` on the properties panel and will call the
equivalent here. Picking what to count next on a slide that cannot be counted on
is an affordance that leads nowhere. The **tallies stay visible and correct** —
a read-only slide still shows its annotations, and the count of what is there is
exactly what a user opening one read-only wants to see.

### 5.2 `ui::categoryTally`

Beside `annotationRows` and `annotationDockTitle` in `ui/AnnotationListPolicy.h`
— already Qt-free, already pure, already covered by ui-tests, already taking
`std::vector<core::Annotation>`:

```cpp
struct CategoryTallyRow
{
    std::string category;   ///< Display text; "Unclassified" for the empty one.
    std::size_t count;
    bool configured;        ///< In the user's list, as opposed to only in the data.
};

[[nodiscard]] std::vector<CategoryTallyRow> categoryTally(
    const std::vector<core::Annotation>& annotations,
    const std::vector<std::string>& categories);
```

Ordering, which is a correctness property rather than a cosmetic one:

- Configured categories first, in list order, **including those at zero**, so a
  category you are about to count shows a row before you start.
- Categories present in the data but not configured next, in first-appearance
  order over `annotations`.
- Unclassified last, and only when non-zero.

**Rows never reorder by count.** A row that jumps while the user is clicking at
it is a misclick, and this is a workflow of several hundred clicks. The same
reasoning already governs `annotationRows`, which breaks every sort tie by
creation order so that rows never swap between rebuilds.

Category matching is case-insensitive, so "mitosis" and "Mitosis" count as one
row, displayed with the configured spelling if there is one and the
first-seen spelling otherwise. The alternative splits a count in half over a
difference the user cannot see.

### 5.3 The tally ignores the filter

The counts are over every annotation in the scene, never over the filtered
subset the list is showing. A clinical count that changed when someone typed in
a search box would be a trap, and the dock title already distinguishes the two
populations — "Annotations (3 of 12)" — so the panel is not silent about
filtering.

## 6. Rename and retag

### 6.1 The gap this closes

Before this round, "fix a wrongly typed category" splits into two operations and
the obvious one does not work:

- **Removing it in Preferences** stops it being offered in both combos, but
  every annotation still carries the string, and by §5.2 a category present in
  the data still gets a tally row. The suggestion disappears; the count stays.
- **Fixing the markers** means selecting each one and retyping Classification in
  the properties panel. `AnnotationModel` holds a single `m_selectedId` and the
  list tree is `SingleSelection`, so FR-ANN-16's multi-select does not exist:
  forty mis-tagged markers is forty selections, forty edits and forty undo
  steps.

That is not a proportionate recovery for the most likely error in a workflow
built on typing a category once and clicking two hundred times.

### 6.2 `core::retagClassification`

A pure function, in core beside `normalizeCategoryList`:

```cpp
struct RetagChange
{
    std::string id;
    core::Annotation before;
    core::Annotation after;
};

[[nodiscard]] std::vector<RetagChange> retagClassification(
    const std::vector<core::Annotation>& annotations,
    const std::string& from, const std::string& to,
    std::chrono::system_clock::time_point modifiedAt);
```

It returns one entry per annotation whose classification matches `from`
case-insensitively, each carrying the exact before and after state. An empty
result means nothing matched, and the caller records no undo step at all —
the same rule `AnnotationEditCommand::create` already applies when it refuses to
build a command that records nothing.

`modifiedAt` is a parameter rather than a clock read inside the function, for
two reasons. The function stays pure and therefore testable, as
`core::shouldAutosave` is for the same reason. And the `after` state must be the
**exact** final state, timestamp included, because `AnnotationEditCommand`
establishes that a command stores whole before and after states and restores
them verbatim. A function that left `modifiedAt` for the model to stamp would
make redo produce a different timestamp from the original action, so undo and
redo would stop being exact inverses.

### 6.3 `app::AnnotationRetagCommand`

A second `ICommand` implementation beside `AnnotationEditCommand`, holding a
vector of `RetagChange` and applying or reverting all of them as one step.

`AnnotationEditCommand` cannot be reused. It is one annotation by construction,
and it carries `m_index` — where the annotation sat in the model — because
restoring a created or deleted annotation at the wrong position changes which
shape paints on top and which one a click finds. A retag changes a property in
place, moves nothing, and needs none of that machinery. Sharing the class would
mean giving every existing command a vector it never uses.

**This rewrites a claim in CLAUDE.md.** The Key Design Decisions entry says one
command type covers create, delete and move "because the inverse is the same
shape in all three". That stays true of those three and of single property
edits; what it does not cover is many annotations in one step. The entry must
say so once this lands, rather than being quietly falsified.

### 6.4 Semantics

- **Scope is the current scene.** Only the open scene's annotations are in the
  model, so only they are retagged. Other slides keep the old string, and the
  spec says so rather than implying a global rename.
- **Renaming onto an existing category merges them.** Two tally rows become one.
  This is the common case — a misspelling of a category that already exists —
  and refusing it would leave the user to retag twice.
- **An empty target is refused.** That would mean "unclassify everything under
  this category", which is a different operation with a different undo
  expectation; overloading rename with it would make one menu item do two things.
- **Unclassified cannot be renamed.** It is the absence of a value, not a value.
  Renaming it would mean bulk-classifying every unclassified annotation on the
  scene — the single most destructive thing this feature could offer — from a
  menu item that reads "Rename".
- **The category list is not on the undo stack.** The list is a global
  application preference shared by every slide, while the undo stack is
  per-slide and cleared on a slide change. Putting the vocabulary on it would let
  an undo on one slide silently revert a change another slide is already using.
  A rename therefore undoes its annotation changes but not its vocabulary
  change: after Ctrl+Z the markers carry the old name and the list still holds
  the new one. This is intended, and is checked by AC28.

### 6.5 Where it lives, and removal

On the tally row's context menu, because the strip is where the typo is
discovered: `Mitossis 1` is visible under the user's eye at the moment the
mistake is made.

- **Rename…** — §6.4.
- **Remove from list** — vocabulary only, never data. Offered for a category
  that is configured; the menu item states how many annotations keep it, so the
  difference between removing the suggestion and fixing the count is visible at
  the point of decision rather than discovered afterwards.

Two interactions with the current category (§4.4), which is independent of the
list and so needs saying rather than inferring:

- **Renaming the current category carries it over**, so counting continues under
  the new name without the user re-picking. Anything else would silently stop
  the markers being counted where the user is watching.
- **Removing the current category from the list leaves it current.** A value may
  be current without being configured — that is already true of one typed in
  §4.5 before its first marker — so removal takes the suggestion away and
  nothing else. Clearing is **(none)**, explicitly.

## 7. Testing

The constraint that shapes this section: no test binary creates a
`QApplication`, so no widget may be constructed in any test. Every rule above
was placed with that in mind, which is why §4.1 is in the model and §5.2 is in
a policy header.

### 7.1 core-tests

- `normalizeCategoryList`: trimming, blank-dropping, case-insensitive
  de-duplication keeping the first spelling, order preservation, an empty input,
  and an input that is entirely blanks.
- `retagClassification`: matches case-insensitively; returns before/after pairs;
  returns empty when nothing matches; leaves non-matching annotations out of the
  result entirely.

### 7.2 app-tests

- `add()` stamps the current category on a `PointGeometry`.
- `add()` stamps nothing on a `RectangleGeometry` or an `ArrowGeometry`.
- An empty current category leaves `classification` empty.
- Changing the current category does not alter any existing annotation.
- `AnnotationRetagCommand` applies every change and undoes every change as one
  step, and the stack depth grows by exactly one.

### 7.3 infra-tests

- `QSettingsAnnotationCategoryStore` round-trips a list through a temp INI.
- An unset store answers the §3.2 defaults.
- A stored list survives a new store object over the same file.

### 7.4 ui-tests

- `categoryTally`: configured order with zeros present; unconfigured categories
  after, in first-appearance order; Unclassified last and only when non-zero;
  case-insensitive merging with the configured spelling winning; an empty
  annotation list; and a tally that is unchanged by what the filter text is,
  since the function does not take one.

### 7.5 Manual — group AC

The strip, both combos, the Preferences row and the context menu are all
unreachable from any suite. This list is written **before** implementation
rather than after: group AB's list found two defects its code review did not,
and the box-resize round recorded that writing the list after the review was the
wrong order.

**Group AC was run in full, 2026-10-18 and 2026-10-19: 28 pass, 1 fail, 1
performable only in part.** It took two sittings. The first was cut short when
the session's other windows began competing for the viewer's focus, and a
further run on 2026-10-19 finished the six checks that were left and re-ran
AC26 against the fixed binary. Every row records what was seen.

Conditions: `Philips-1.tiff` (27 annotations) and `S1303802-11-HE-DX1.svs`
(empty, used for AC22 and AC23), workspace `D:/tmp/third`, configured categories
`Tumor | Apoptosis | Ki67`. The fixtures and the configured list were restored
afterwards.

| | Check | Result |
|---|---|---|
| AC1 | Preferences shows the five default categories on a profile that has never set them. | **Pass** |
| AC2 | Edit the list in Preferences, reopen the dialog: the edit persisted. Restart the application: still persisted. | **Pass** — both halves; the restart half was confirmed on 2026-10-19 with `Tumor, Apoptosis, Ki67` surviving an application restart. |
| AC3 | A list entered with blank lines, surrounding spaces and a duplicate differing only in case comes back trimmed, de-duplicated and in the order typed. | **Pass** — `  Mitosis  / Lymphocyte / (blank) / MITOSIS /   Tumor  ` came back as exactly `Mitosis, Lymphocyte, Tumor`: trimmed, blank dropped, case-duplicate removed keeping the first spelling, order preserved. |
| AC4 | The properties panel's Classification combo offers the same list as the counting combo, not the old hardcoded five. | **Pass** |
| AC5 | Pick a category, place five markers: all five carry it, in the list's Classification column and in the properties panel. | **Pass** |
| AC6 | With a category current, draw a rectangle and an arrow: both are unclassified. | **Pass** |
| AC7 | Change the current category: the five already placed are unchanged. | **Pass** |
| AC8 | Select **(none)**, place a marker: it is unclassified and the Unclassified tally row appears. | **Pass** |
| AC9 | The tally updates as each marker is placed, and the list below neither scrolls nor loses its selection while it does. | **FAIL** — the tally half is right; the list half is not. See below. |
| AC10 | Configured categories show rows at zero before counting starts. | **Pass** — seen again on switching to an empty slide, where all four configured rows read 0. |
| AC11 | Type a category not in the list: it becomes current but does **not** join the list until the first marker is placed under it. | **Pass** — checked against the registry rather than the dialog: after typing `Apoptosis` the stored list was unchanged; after the first marker it held it. |
| AC12 | After that marker, it appears in Preferences and in the properties combo. | **Pass** — `CD31`, typed during AC26, was listed in Preferences afterwards. |
| AC13 | Undo that marker: the category stays in the list and its tally row stays, reading zero. | **Pass** |
| AC14 | Type into the list's filter box: the tally rows do not change. | **Pass** — every count held while the list below filtered down. |
| AC15 | Count under two categories, then rename one onto the other: the rows merge and the sum is right. | **Pass** — Lymphocyte (5) renamed onto Mitosis (3) gave one row, **Mitosis 8**. |
| AC16 | Ctrl+Z after a rename restores every annotation's old category in one step; Ctrl+Y reapplies it in one. | **Pass** |
| AC17 | Rename with an empty target is refused, and says why. | **Pass** |
| AC18 | Unclassified offers no Rename. | **Pass** — and separately, renaming a real category *to* "Unclassified" is refused with "Unclassified is not a category. Nothing was renamed." with the stored list untouched. That is the round's one Critical finding, confirmed fixed in the running application. |
| AC19 | Remove from list on a category in use: the menu item states the count, the suggestion disappears, the annotations and their tally row remain. | **Pass** |
| AC20 | Close the slide and reopen: every category comes back from the file, tallies rebuild, `schemaVersion` still reads 1. | **Pass** — Ctrl+W then reopen from Recent Files brought back all 37 annotations with the tally rebuilt (10+1+1+2+8+15), and the file still read `"schemaVersion": 1`. See the note below on what made this look broken the first time. |
| AC21 | Switch to another slide: the current category is still selected. Restart the application: it is not. | **Pass** — both halves. Switching from the TIFF to the SVS kept `Tumor` current; after a restart the combo read **(none)**. |
| AC22 | On a read-only slide the counting combo is disabled, as the drawing tools already are, and the tally rows still show the correct counts. | **Pass** — set up by the box-resize spec's recipe (point the stored `slideId` at a value matching no slide, answer the mismatch dialog Open Read-Only). The status bar read "Annotations disabled", the tally still read Tumor 10, and the combo refused both focus and typing: clicking it produced no caret and `ZZZ` did not reach it. |
| AC23 | Place 60 markers, then undo 50: the stack stops at its limit without losing the tally's agreement with the list. | **Pass** — 60 markers all counted under Tumor; 50 undos left exactly 10, tally and list agreeing; four further undos changed nothing, so the stack stopped at its limit rather than running past it. |
| AC24 | Rename the current category: counting continues under the new name with nothing re-picked. Remove the current category from the list: it stays current, and (none) is what clears it. | **Pass** |
| AC25 | Clear the category list in Preferences, restart the viewer: it stays empty and the five defaults do not come back. (The empty-list round-trip is proven only on the INI backend the tests use; production uses the native registry backend.) | **Pass**, and this is the one the final reviewer most expected to fail. Clearing the field wrote `categories` to the registry as an empty `String[]`, and after a restart Preferences was still empty and the combo offered only **(none)**. Decisively, the tally then showed *only* unconfigured rows in first-appearance order — Mitosis 8, Ki67 1, Apoptosis 1, Tumor 10, CD31 2, Unclassified 15 — with no zero rows at all, which is what the defaults coming back would have produced. The empty-list round-trip now holds on the real backend. |
| AC26 | Place the first marker under a freshly typed category, then open the counting combo's dropdown, dismiss it with Escape, and press Down then Up: the counting category and the next marker's classification are unchanged. | **Failed on 2026-10-18, fixed, and re-run green on 2026-10-19.** See below. |
| AC27 | The same as AC26, with a category typed in a different case from a configured one (for example `tumor` against `Tumor`). | **Pass** — both halves. Typed `tumor` corrected itself to the configured `Tumor`, counted there and added no duplicate; two Downs and an Up then left it on Tumor and the next marker counted under Tumor. This is the harder case, since Tumor is a real list item the arrows would otherwise step away from. |
| AC28 | Rename a category and press Ctrl+Z only, not Ctrl+Y: the annotations revert and the category list stays renamed. This is intended, not a defect (§6.4). | **Pass** — and Lymphocyte came back as an *unconfigured* tally row, which is what §6.4 predicts. |
| AC29 | Right-click a tally row: the menu appears at the row under the cursor. The keyboard Menu key opens it on the selected row. | **Partial — the mouse half passes; the Menu-key half is not performable as written.** See below. |
| AC30 | The transient refusal messages (empty rename target; "Unclassified is not a category") reach the status bar and are legible there. | **Pass** |

#### AC26 — the failure this list existed to catch

After typing a category and counting one marker under it, opening the dropdown
and pressing Down changed the combo, and the next marker was counted under the
category Down had landed on. A three-step probe isolated the cause: after typing
`CD31` the combo read `CD31`; after opening the dropdown and dismissing it with
**Escape** it still read `CD31`; after **Down** it read `Ki67`. The arrow key is
the culprit and Escape is innocent — so the check's own wording, which blames
the dropdown, points at the wrong gesture.

The cause is ordinary Qt: a closed editable `QComboBox` treats Up, Down, PageUp
and PageDown as "step to the next item", replacing the line edit's text. That is
correct for a combo and wrong for this one, because this combo's value is a
clinical counting category and nothing announces that it moved. The user of a
several-hundred-click pass reads the slide and the tally, not the combo, and the
tally — correct about what was actually stored — agrees with the mistake. No
reviewer would have flagged the behaviour as wrong, and no test could reach it.

Fixed by `ui::countingComboSwallowsKey` and `ui::countingComboSwallowsWheel`
(`AnnotationListPolicy.h`), asked by an event filter on the combo and its line
edit. The keys are swallowed only while the popup is CLOSED, so deliberate
keyboard selection — F4 or Alt+Down to open, arrows to walk, Enter to pick — is
untouched; inside an open popup the arrows move a highlight and commit nothing.
The wheel is swallowed by the same rule and for the same reason: over a closed
combo it steps items exactly as an arrow key does, and a hand resting on a
trackpad while the cursor sits over the strip is at least as likely as a bumped
arrow key. The decision is two pure predicates because the widget is unreachable
from any suite; the filter does nothing but ask them.

**Re-run 2026-10-19 against the fixed binary:** typed `CD31`, counted one marker
(CD31 1), opened the dropdown, Escape, Down, Up — the combo still read `CD31`
and the next marker made it **CD31 2**, with Ki67 and Apoptosis untouched. The
wheel was checked the same way: four wheel clicks over the closed combo left it
on Tumor and the next marker counted under Tumor.

#### AC9 — the failure that is still open

The tally half is right: three markers moved Tumor 5 → 8 and the total 32 → 35,
every count correct. The list half is not. With the list deliberately scrolled
up to show `Point 4 … Arrow 11` and **Arrow 6 selected**, placing two markers
jumped the list to the bottom (`Point 30 … Point 37`) and the selection was
gone — the properties panel then showed the *new* marker's `Tumor`
classification, not Arrow 6's empty one.

So the cause is not the tally rebuilding: **creating an annotation selects it**,
and the list scrolls to show its selected row. That behaviour predates this
round and is reasonable on its own — you may well want to label the shape you
just drew. It is wrong for *this* workflow, which is the one the round exists
to serve: a counting pass is several hundred creations, and each one yanks the
list away from wherever the user put it. Nothing is miscounted and nothing is
lost, so this is friction rather than a data defect, which is why it is recorded
here rather than fixed in haste. Fixing it means deciding whether a point marker
placed under a current category should select at all — the counting gesture is
arguably a tally increment, not a selection — and that is a design question this
spec did not ask.

#### AC29 — not performable as written

The mouse half passes. The Menu-key half cannot be performed, and the reason is
in the design rather than the build: the tally is created with
`setSelectionMode(NoSelection)` and `setFocusPolicy(Qt::NoFocus)`
(`AnnotationListPanel.cpp:81-82`). A widget that never takes focus never
receives `Qt::Key_Menu`, and one with no selection has no "selected row" for the
menu to open on. Sending the key with the viewer focused produced nothing, as
those two lines predict.

The check assumes a focusable, selectable tally; the strip is deliberately
neither, because it is a readout rather than a control. This is the same shape
as group AB19 and AB23 — a check written against an intention the code
deliberately does not have. Either the tally gains focus and selection, or the
check should be struck; it should not sit here looking like a defect.

#### AC20 — why it looked broken the first time

On 2026-10-18 this read as a failure: after Ctrl+W and a reopen the panel showed
**Annotations (0)** while the file held 27. The reopen was never the problem.
**The window title did not clear when a slide was closed** — after Ctrl+W it
still read `SlideIO Viewer - …\Philips-1.tiff`, so a correctly closed viewer
looked like a viewer that had failed to reopen. The File menu settles it: with
the slide closed, `Close  Ctrl+W` is greyed out. Reopening from Recent Files
then restored all 37 annotations and the full tally.

Three cosmetic defects fell out of running it, none affecting data. All three
were fixed on 2026-10-19, after the group was complete; each had a different
cause, and none of the three was where the symptom pointed.

- **The title kept naming a closed slide.** `MainWindow::openSlide` sets the
  title and nothing ever unset it. It is cleared in the Close action's handler
  and after a failed open — deliberately *not* from the `slideClosed` signal,
  which also fires at the start of every open: the title is set before the
  viewport closes the previous slide, so clearing there would blank it on every
  open rather than only on a close.
- **The Scenes panel did not repopulate after a close-and-reopen within one
  session.** The `slideOpened` handler skips the panel rebuild when the file has
  not changed, so that clicking a scene thumbnail does not clear the strip it
  was clicked in. The Close action emptied both panels but left
  `lastOpenedFilePath` naming the file it had just emptied them of, so reopening
  that same slide took the skip and left them empty. Clearing the memo beside
  the panels restores the invariant: it names the file the panels hold, and they
  hold nothing.
- **Recent Files listed `Philips-1.tiff` twice**, at positions 1 and 3. The two
  entries were the same file in two spellings — `D:\…\Philips-1.tiff` from the
  shell's command line (`main.cpp` passes `argv[1]` through unchanged) and
  `D:/…/Philips-1.tiff` from the file dialog — and the menu labels an entry with
  its last component alone, so one slide read as two. De-duplication was an
  exact `QStringList::removeAll`, which the two spellings slipped past. The rule
  moved into `ui::recentFilesWithPathPromoted`, where a test can reach it: it
  normalizes separators, promotes, de-duplicates and truncates, and normalizes
  the stored entries on the way through, so a list that already holds both
  spellings is repaired the next time it is written rather than needing a
  migration. Separator style and case fold only on Windows — on a
  case-sensitive platform a backslash is an ordinary file-name character, and
  folding either would rewrite a stored path into one that no longer opens.

#### Notes for whoever runs this again

- The Preferences dialog warps the cursor out of its own text box to a fixed
  point when driven by synthetic mouse events. Drive that dialog from the
  keyboard: Tab three times from the workspace field reaches the categories box.
- Non-focusing synthetic keystrokes can miss the application entirely, and will
  land in whatever window holds the foreground. One such miss produced a false
  "undo does not work", disproved by opening the Edit menu and reading "Undo
  Draw Point" enabled with Redo greyed. Confirm through the menu before
  believing a keyboard-driven negative, and check the foreground window before
  sending keys at all.
- AC22 needs `annotationsEditable()` false. The cheapest route is the box-resize
  spec's: edit the stored `slideId` to a value matching no slide, reopen, and
  answer the mismatch dialog **Open Read-Only**. Restore the file from a
  byte-identical backup afterwards.
## 8. Deliberate absences

- **Export or report of counts.** The obvious next round, and the one a
  pathologist will ask for after using this. Left out so this round ships the
  counting itself.
- **Per-tool default classification (FR-ANN-22).** The current category is one
  value, not one per tool. Still open.
- **The quick-classification bar (FR-ANN-21).** Still open.
- **A keyboard shortcut to cycle categories.** Plausible for a heavy counting
  pass; no evidence yet that it is needed, and it would need a visible indicator
  of what cycled to.
- **The confidence field (FR-ANN-14).**
- **Multi-select and bulk edit (FR-ANN-16).** §6 closes the specific gap that
  made its absence painful here; it does not substitute for it.
- **Per-area density or statistics.** Counting, not analysis.

## 9. Risks and accepted trade-offs

| Risk | Disposition |
|---|---|
| One axis means a marker cannot be both a mitosis and in stroma | Accepted and named (§2.1). Revisit if a second axis is ever asked for; it would be a new persisted field and a format decision. |
| The Annotation List panel takes a second responsibility | Accepted when the placement was chosen; the strip is small and the policy lives outside the panel. |
| A typo becomes a configured category | Mitigated three ways: it only joins on first use (§4.5), it is visible as its own tally row, and §6 renames it and its annotations together. |
| A rename touches only the open scene | Named in §6.4 rather than hidden. A study-wide rename would need every scene loaded and is not this round. |
| 50-step undo during a long counting pass | Accepted, as the point/arrow round accepted it. FR-ANN-18's floor is met, and removing a misplaced marker is a click rather than an undo of sixty. AC23 checks the limit behaves. |
| A second `ICommand` type weakens the "one command type" claim | Accepted and handled: CLAUDE.md is corrected as part of the round (§6.3), not left to be falsified silently. |

## 10. Requirements

| Requirement | Status |
|---|---|
| FR-ANN-13 — user-configurable classification categories | **Complete** with this round |
| FR-ANN-11 — classification category per annotation | Already met; this round makes it usable at scale |
| FR-ANN-16 — multi-select | Still open; §6 closes the gap that made it urgent |
| FR-ANN-21 — quick-classification bar | Still open; §8 |
| FR-ANN-22 — per-tool default classification | Still open; §8 |
| FR-ANN-18 — 50 undo levels | Unchanged and met; §9 |
