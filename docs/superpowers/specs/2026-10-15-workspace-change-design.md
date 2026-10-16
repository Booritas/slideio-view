# Workspace Change: Annotation Handling

**Sub-project H of Stage 3.** Spec written 2026-10-15.

## Intent

Changing the annotation workspace folder changes where annotations are read
from and written to. Today it does that silently: the annotations on screen
disappear, the viewer says nothing, and the user is left to work out that their
work is still in the folder they just left. Observed 2026-10-15 during the
group W campaign — an annotation was created, labelled, the folder changed, and
the annotation vanished. Nothing had been lost, but nothing said so.

This sub-project makes the switch an explicit, informed decision: what is on
screen, what is in the new folder, and what will happen to each.

## Scope

In scope:

- A warning and choice when the workspace changes while a slide with
  annotations is open.
- Carrying the open scene's annotations into the new workspace, by copy or by
  merge.
- A pure merge of two annotation sets, with a reported receipt.

Out of scope, deliberately:

- **Bulk migration.** Only the open slide and the open *scene* travel. A slide
  with annotations on three scenes carries one; other slides carry none. The
  dialog says so rather than implying otherwise. Moving a whole store is a
  separate feature with its own progress and conflict reporting, and it should
  not be smuggled in behind a settings change.
- Any change to `schemaVersion`, the file format, or the file naming scheme.
- Any write to the old workspace beyond the flush that already happens there.
  **The old folder is never deleted from and never rewritten.**

## 1. Order of operations

A new `app::WorkspaceSwitch` owns the sequence. It holds references to the
repository, the model and `AnnotationPersistenceService`, plus two callbacks
standing in for the repository's root (see §5).

```cpp
enum class WorkspaceChoice { Copy, KeepCurrent, Merge, LoadNew, Cancel };

using WorkspaceChangeResolver =
    std::function<WorkspaceChoice(const core::WorkspaceSwitchFacts&,
                                  const core::WorkspaceSwitchOptions&)>;

WorkspaceSwitchResult WorkspaceSwitch::run(const std::string& newRoot,
                                           const core::AnnotationKey& key,
                                           const core::SlideProvenance& provenance);

struct WorkspaceSwitchResult
{
    WorkspaceChoice choice = WorkspaceChoice::Cancel;
    bool asked = false;                 ///< false when no dialog was warranted
    bool rescued = false;               ///< the save-failure resolver re-pointed the workspace
    core::SaveStatus flush = core::SaveStatus::NothingToDo;  ///< how step 1 went
    core::MergeOutcome merge;           ///< zeroed unless choice == Merge
    core::SaveStatus write = core::SaveStatus::NothingToDo;
    std::string writePath;              ///< empty when nothing was written
};
```

`ui` uses the result for one thing: the status-bar message. It does not re-derive
what happened.

The order is the design:

1. **Flush to the old root** — `persistence.endSlide()`, as `openPreferences()`
   already does. Whatever is on screen is also on disk in the old folder before
   anything else happens.
2. **Capture** the in-memory annotations.
3. **Re-point the repository to the new root and `load(key)`.** That load is
   the probe. Everything written after this point goes to the new folder.
4. **Build the facts** (§3) from the capture and the probe.
5. **Ask the resolver**, which is `ui`'s dialog. Asked *after* the probe, so it
   states real counts rather than guesses.
6. **Apply.** `Copy` and `KeepCurrent` write the captured set; `Merge` writes
   the merged document; `LoadNew` writes nothing; `Cancel` re-points back to the
   old root.
7. **`beginSlide(key, provenance)`** against whichever root is now in effect.
   The model reloads from the file. The file stays the single source of truth —
   the model is never hand-patched.

### Why this does not reopen the scene

`openPreferences()` currently ends in `reopenCurrentScene()`, which re-reads the
image. A workspace change affects no pixel. Because `WorkspaceSwitch` owns the
service, step 7 is a plain `beginSlide`: annotations reload, tiles are
untouched, and the `openScene` → `closeSlide` path drops out of this feature.
That path is where the 2026-10-15 pending-edit ordering defect lived
(`9ec4ced`); this feature does not re-enter it.

It also makes `Cancel` correct. Step 1 deactivated the service. Without a
`beginSlide` to follow it, a cancelled switch would leave a slide open that
silently stops saving — a failure invisible until the user's next edit
disappeared.

## 2. The merge — `core`, pure, Qt-free

```cpp
struct MergeOutcome
{
    std::vector<Annotation> annotations;
    int adopted = 0;    ///< ids only the incoming file had
    int superseded = 0; ///< ids in both; the incoming one was newer and won
    int kept = 0;       ///< ids in both; the on-screen one was newer or tied
};

[[nodiscard]] MergeOutcome mergeAnnotations(const std::vector<Annotation>& current,
                                            const std::vector<Annotation>& incoming);
```

Union by id. Where both sets hold an id, the later `modifiedAt` survives.

**The comparison truncates both sides to whole seconds**, because that is the
precision the file format carries — `formatIso8601Utc` casts to
`std::chrono::seconds` (`src/core/src/Iso8601.cpp:107`). The on-screen
annotation still holds its sub-second value; the one loaded from the new folder
lost its on the way to disk. Comparing them as they are would favour whichever
happens to be in memory, and would give a *different answer after a round-trip
through a file*. Merging the same two sets twice must produce the same result.
On a tie the on-screen annotation is kept: deterministic, and it is the one the
user is looking at.

**Order is preserved deliberately**, because index order is paint order
(`paintAnnotations`) and hit-test order. The current set keeps its sequence; a
superseded annotation's replacement takes the slot the old one held, so nothing
jumps in z-order; ids only the incoming file had are appended, landing on top.

No deduplication by geometry. Two identical shapes under different ids are two
findings — possibly from two readers — and this is not the place to decide they
are one.

`mergeAnnotations` takes and returns annotations only, not documents, so it
stays a function of exactly the data it reasons about. The merged *document's*
`createdAt` is the earlier of the two documents' — the set's history starts
there — and `WorkspaceSwitch` applies that in step 6, where both documents are
in hand.

Annotation ids are UUID v4 (`AnnotationModel::generateId`), so two
independently drawn sets never collide. A collision means one file descended
from the other — a previous copy, then edits on both sides — which is exactly
the case the `modifiedAt` rule is for.

## 3. What is offered, and what the dialog says

```cpp
struct WorkspaceSwitchFacts
{
    int currentCount = 0;          ///< annotations on screen
    bool currentWritable = true;   ///< false when read-only or unidentified
    bool flushFailed = false;      ///< step 1 could not write the old folder
    std::string oldPath;           ///< where the current set was just flushed
    bool newFileExists = false;
    bool newFileReadable = true;   ///< false when malformed or a future version
    int newCount = 0;
    std::string newPath;
};

struct WorkspaceSwitchOptions
{
    bool ask = false;         ///< false means switch without a dialog
    bool copy = false;
    bool keepCurrent = false;
    bool merge = false;
    bool loadNew = false;
};

[[nodiscard]] WorkspaceSwitchOptions workspaceSwitchOptions(const WorkspaceSwitchFacts&);
```

| Situation | Offered |
|---|---|
| Nothing on screen, no file in the new folder | no dialog — just switch |
| Nothing on screen, file in the new folder | no dialog — switch and load it; nothing is at risk |
| On screen, new folder empty | **Copy** · Switch without copying · Cancel |
| On screen, new folder has a readable file | **Merge** · Keep what's on screen · Use the new folder's · Cancel |
| On screen, new folder's file unreadable or a newer schema | Use the new folder's · Cancel, with the reason stated |
| On screen but read-only (slide-id mismatch, unidentified slide) | Use the new folder's · Cancel, with the reason stated |

Cancel is always available; it is the Esc button. The default is the option
that destroys least — **Merge** where offered, **Copy** otherwise.

An unreadable file in the new folder withholds every write: this codebase does
not write over a file it could not parse, and `Copy`, `KeepCurrent` and `Merge`
all would.

A **failed flush never withholds an option.** If the old folder was unwritable
and the user is switching away *because* of that, `Copy` and `Merge` are the
rescue. The facts carry `flushFailed` so the dialog can say the current folder
could not be written, and the captured set is still in memory either way.

### The dialog

`ui::WorkspaceChangeDialog`, alongside `SlideProfilesDialog` and
`PreferencesDialog` — four buttons with explanatory sub-text is past what a
`QMessageBox` does well. It is built entirely from the two structs above and
returns the enum. It holds no logic.

> **The new annotation folder already has annotations for this slide.**
>
> On screen now: **12** — saved in `F:\Documents\SlideIO Viewer\annotations`
> In the new folder: **5** — `D:\tmp\annotations\8667bdde….json`
>
> Annotations are read from the folder in use, so changing it replaces what you
> see. Nothing is deleted from the current folder. Only the scene you are
> viewing is carried over; other scenes and other slides stay where they are.

Under *Keep what's on screen*: "replaces the 5 annotations in the new folder's
file". The overwrite is named before it happens.

After the switch the status bar carries the receipt: *"Merged: 3 added, 1
superseded, 12 kept."* A merge that silently changes a set of clinical findings
is not acceptable; the count is the receipt.

## 4. Edge cases

- **The save-failure rescue wins.** `endSlide()`'s flush can fail, and the
  existing `SaveFailureResolver` may rescue the save to a folder the user picks
  mid-failure — which it then makes the workspace. That folder beats the
  dialog's entry, as it does today: the switch is abandoned, no probe, no
  dialog, and the existing message names the folder in use. **`run()` detects
  this itself**, by comparing the root across step 1's flush, because it owns
  that flush — `openPreferences()` must not flush as well, since `run()` reads
  `isActive()` beforehand to decide whether the annotations may be written at
  all, and a prior flush would make every switch look read-only.
- **Undo stack: cleared on every switch**, including `Copy`, where the set does
  not change. A command recorded against one store, replayed after the store
  moved, would re-add into a folder it was never recorded against. One rule,
  stated in the dialog.
- **No slide open, an associated image, or an unidentified slide**: no dialog;
  re-point and save the setting.
- **The write to the new folder fails**: nothing is lost — step 1 already put
  the current set in the old folder — and the existing save-failure resolver
  handles it as it does anywhere else.
- **The entered folder resolves to the one already in use**: unchanged
  behaviour, `openPreferences()` returns early before any of this.
- The pending-edit commit added in `9ec4ced` stays where it is, ahead of
  step 1.

## 5. Testing

### The repository root seam

`core::IAnnotationRepository` exposes `load`, `save` and `pathFor` only.
`setWorkspaceRoot` is on the concrete `infra::JsonAnnotationRepository`, which
`app` must not reach into, and a filesystem root does not belong on the core
interface — a remote repository has none. `WorkspaceSwitch` therefore takes
`std::function<std::string()> currentRoot` and
`std::function<void(std::string)> repointTo`, the same `std::function` seam the
codebase already uses for `MismatchResolver`, `SaveFailureResolver` and
`onStackChanged`. In tests they close over a string beside the fake repository.

### Automated — `core-tests`, `mergeAnnotations`

- Disjoint sets: union, `adopted` counts the incoming, order is current then
  incoming.
- Same id, incoming newer: incoming survives **in the current one's slot**,
  `superseded` is 1.
- Same id, current newer: current survives, `kept` is 1.
- Same id, timestamps equal: current survives, `kept` is 1.
- **Same id, timestamps differing only below the second**: treated as equal,
  current survives. This is the round-trip reproducibility rule.
- Empty current, empty incoming, and both.

### Automated — `core-tests`, `workspaceSwitchOptions`

One case per row of the matrix in §3, including both `ask == false` rows, the
unreadable file, and the read-only slide.

### Automated — `app-tests`, `WorkspaceSwitch`

The existing `FakeRepository` in `tests/app/AnnotationPersistenceServiceTest.cpp`
is file-local and root-blind: its `pathFor` ignores any root and it holds one
`nextLoad`. These tests need a fake whose contents depend on the root in effect,
so the new test file defines its own — storage keyed by (root, key), so a save
to one root is invisible to a load from another. The existing fake is left
alone; two small fakes with different jobs beat one shared fake with a flag.

The sequence is what is under test:

- The old root receives the flush **before** the re-point; the probe happens
  **after** it.
- `Copy` writes the captured set to the new root.
- `Merge` writes the merged document to the new root.
- `LoadNew` writes nothing.
- **`Cancel` re-points back, restores the model, and leaves the service active
  and saving.**
- An unreadable file in the new root never receives a write.
- A failed flush still offers `Copy`, and `Copy` still writes.
- The resolver is not called in either no-dialog row.
- After every outcome the service is active and keyed to the root in effect.

### Manual — group X

The dialog cannot be constructed in any test, since no test binary creates a
`QApplication`. Run against an installed binary, with the viewer killed before
`cmake --install` and the install verified by hash or timestamp.

| # | Check |
|---|---|
| X1 | New folder empty, Copy: the annotations are in the new folder's file and still on screen; the old folder's file is unchanged |
| X2 | New folder has annotations, Merge: counts in the status-bar receipt match the file on disk, and a superseded annotation kept its z-order |
| X3 | Keep what's on screen: the dialog names the right path and count before the overwrite, and the file afterwards holds exactly the on-screen set |
| X4 | Use the new folder's: the new folder's file is untouched and the old folder still holds the previous set |
| X5 | Cancel: the setting is unchanged, the annotations are unchanged, **and a subsequent edit still autosaves** — the check that catches a service left deactivated |
| X6 | A deliberately corrupted file in the new folder: only Use-the-new-folder's and Cancel are offered, and no write reaches that file |
| X7 | A read-only slide (slide-id mismatch answered ReadOnly): the dialog says the annotations are not carried over, and offers no write |
| X8 | No slide open: the folder changes with no dialog at all |
| X9 | **The slide does not visibly reload** during any switch — no tile flicker, no loading overlay. The only evidence the switch stopped going through `closeSlide()` |
| X10 | A slide with annotations on two scenes: only the open scene's file travels, and the dialog said so |
| X12 | **Put a deliberately corrupted file for the open slide in the new folder and switch to it.** The refusal dialog appears *and* the three drawing tools go disabled, the properties fields grey out, and the status bar says annotations are read-only. Before the fix the tools stayed enabled and anything drawn afterwards was accepted by the model, never written, and gone at the next close |
| X13 | **Open an associated image (a label or macro thumbnail) from the Associated Images panel, then change the workspace folder.** The dialog appears and behaves exactly as X1, because the preview is a separate `AssociatedImageWindow` and the slide is still what the viewport holds. The preview window is untouched by the switch. **This is not the guard's check** — see the note under Results: the viewport state `isAuxImageShown()` tests cannot currently be reached from the UI at all |
| X14 | **Make both the old and the new folder unwritable, then switch and choose Copy.** A *critical dialog* — not a transient line — says neither folder could be written, and the annotations are still on screen. The old message claimed they were safe in the folder you switched from, which in this case is false |
| X15 | **Make the old folder unwritable, draw something, switch, and press Cancel.** What is on screen must be exactly what was there before the dialog, including the annotation that never reached disk. Before the fix, Cancel reloaded the stale file over it |
| X16 | On the X12 dialog (only "Use the new folder's" and Cancel offered), press **Enter**. Nothing destructive happens — Cancel is the default. Without an explicit default the first auto-default button wins, and that is the switch |
| X17 | **Open a slide from a slow source and, while it is still loading, change the workspace folder.** The change is refused with "a slide is still opening", and nothing is written. The hazard is not theoretical: the open completes on a queued connection, which runs inside a modal dialog's event loop, so without the guard the open would swap the model out from under a switch that had already captured the previous slide's key — and that slide's annotations would end up loaded over this one |
| X11 | Undo is empty after a switch — draw something, switch, and confirm Edit ▸ Undo is disabled. No new code clears it; the clear rides on the `modelReset` that `beginSlide` already emits, so this is the check that the existing listener still fires on a path that no longer goes through `closeSlide()` |

### Results — run 2026-10-16 against `main` @ `ca90353`

**Passed:** X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15 — X13
rewritten after the run, for the reason below.

X1-X5 are the five outcome checks: Copy into an empty folder, Merge, Keep what's
on screen, Use the new folder's, and Cancel. Together they cover every branch of
`applyChoice` and both write paths, so the sequence in `app::WorkspaceSwitch` is
exercised end to end against a real repository rather than against the fake.

X6 and X7 are the two rows of the options matrix that withhold a write. X6 — a
corrupted file in the new folder — is the one that proves "a file we could not
parse is never a file we write to" survives this path: the probe load fails,
`workspaceSwitchOptions` clears `copy`, `keepCurrent` and `merge`, and the
dialog has nothing constructive left to offer. X7 is the same withholding
reached from the other direction, where it is the annotations on screen that
cannot be trusted rather than the ones on disk: a slide-id mismatch answered
ReadOnly leaves `currentWritable` false, and a read-only set is not carried into
a folder where it would look authoritative.

X8 is the no-dialog row — nothing on screen, nothing to warn about. It is worth
having run because the feature's whole risk is asking a question nobody needed:
a user who changes the folder with no slide open must not meet a dialog at all,
and `workspaceSwitchOptions` returning `ask == false` on `currentCount == 0` is
what keeps the old silent behaviour exactly where it was already right.

X9 is the one check that tests the design rather than the feature. "No tile
flicker, no loading overlay" is the only externally visible evidence that the
switch stopped routing through `closeSlide()` and `reopenCurrentScene()` — see
*Why this does not reopen the scene* above. Nothing in any suite can see this:
the reopen it replaced would have been correct in every assertion the automated
tests make and merely wrong in the viewport. It passing means the annotation
layer is now swapped underneath a slide that stays loaded, which is what makes
a folder change feel like changing a folder rather than reopening the case.

X10 confirms the scope the user chose: annotation files are keyed by (slide id,
scene index), the switch holds one key, and so exactly one file travels. The
check has two halves and both matter — the other scene's file stays where it
was, *and the dialog said so* before the choice was made. A user who assumed
"copy" meant the whole slide would otherwise find out afterwards, which is the
wrong time.

X11 is the counterpart to X9, and the two are in tension: X9 wants the switch
to skip `closeSlide()`, and `closeSlide()` is where the undo stack used to be
cleared. No new code replaced it. The clear rides on the `modelReset` that
`AnnotationModel::replaceAll` emits from inside `beginSlide`, and X11 is what
proves that existing listener still fires on a path that no longer goes through
`closeSlide()`. Had it not, Ctrl+Z after a switch would have undone an edit
against annotations the model no longer holds.

X12 is the first of the review findings to be verified, and the worst of them.
X6 already showed the refusal dialog is right; X12 asks the question X6 cannot
— what the application lets you do *afterwards*. Before the fix the drawing
tools stayed enabled against a service that was no longer active, so every
annotation drawn after a refused switch was accepted by the model, never
written, and gone at the next close: work silently discarded, with nothing on
screen saying so. It passes because availability is now driven by
`AnnotationPersistenceService::activeChanged` as well as by
`ViewportWidget::annotationsAvailableChanged`, the latter having been the only
source and only re-emitted by the `reopenCurrentScene()` this design removed.

X5 is the one with teeth among them. "A subsequent edit still autosaves" is the
only check that can catch a service left deactivated on the Cancel path, and a
deactivated service is exactly what the original defect produced — the switch
used to call `endSlide()` and never come back, so everything drawn afterwards
was accepted by the model, never written, and gone at the next close. It
passing means `beginSlide` is reached on the abandoned path too.

X14 is the one where the fix was to a *message* rather than to behaviour, and
it is still the finding with the worst consequence of the three the review
graded Important. With both folders unwritable and Copy chosen, nothing can be
written anywhere, and the old transient line told the user their annotations
were safe in the folder they had just switched away from — a false statement
about where someone's work is, delivered in the one place they would not look
twice. It now raises a critical dialog saying neither folder could be written,
and the annotations stay on screen, unsaved and visibly so. Reaching this at
all takes a deny ACE on each folder; the Windows read-only attribute is ignored
for directories, and denying only the workspace root is a no-op once its
`annotations` subfolder exists, since `QDir::mkpath` succeeds on a folder that
is already there.

X15 is the one that was losing work rather than describing it wrongly. Cancel
has to put back what the dialog interrupted, and after a failed flush the file
on disk is *older* than the screen — so reloading it, which is what Cancel used
to do, overwrote the annotation that had never reached disk with the version
that predated it. The user's own gesture destroyed their work, which is the
worst shape a defect can take: refusing a change is supposed to be the safe
answer. The fix restores from the captured in-memory set instead
(`m_model.replaceAll(current)` plus `markDirty()` when `facts.flushFailed`),
and X15 passing means the annotation that existed only on screen survived the
round trip. Nothing automated could have caught this -- the app test proves the
restore happens, but only a human can confirm the shape on screen is the one
that was there before the dialog opened.

**X16 failed on this build and was fixed.** "Use the new folder's" was the
default button despite `cancel->setDefault(true)`, so Enter on the refusal
dialog did the destructive thing. `setDefault()` was the wrong instrument:
`QPushButton::autoDefault` is on for every button in a dialog, and an
auto-default button **takes over as the default while it has focus**. Nothing
gave Cancel focus, so focus went to the first button in the box — the switch —
and it became the default regardless. Focus decides; `setDefault` only decides
among unfocused buttons. Both branches now set focus along with the default,
which also removes the constructive branch's unstated dependence on
`QDialogButtonBox` laying the preferred button out first. No test can reach
this: the dialog cannot be constructed without a `QApplication`, so X16 re-run
is the only gate.

**X13 was asking for a state the UI cannot reach.** Run as written — open a
thumbnail from the Associated Images panel, then change the folder — the dialog
appeared, Copy worked, and the annotation stayed on screen. That is correct, and
the check was wrong to expect silence. The panel's handler calls `loadAuxImage`,
takes the `QImage` back and pops a separate `AssociatedImageWindow`; the slide
never leaves the viewport. Commit `1d4fe92` made it so deliberately.

The viewport-level aux state the guard tests comes from
`ViewportWidget::openAuxImage`, and that is a closed loop:
`currentAuxImageName` is assigned in exactly one place
(`ViewportWidget.cpp:2419`, inside `openAuxImage`), and `openAuxImage` has
exactly one caller — `reopenCurrentScene` at line 2801, inside
`if (!auxName.empty())`. The only setter runs only when the flag is already
set, so it can never go from empty to non-empty. **`isAuxImageShown()` is
always false today, and the guard in `openPreferences` protects nothing.**

The guard stays. `openAuxImage` is still public API and `reopenCurrentScene`
still branches on it, so the dead end is in the wiring, not in the widget; if
an aux image is ever shown in the viewport again the reasoning in that comment
holds exactly as written — it answers `isSlideOpen()` with true and carries the
file's slide id with scene index 0. What changes is the claim about it: it is
insurance against a path that is currently unreachable, not a fix for a defect
anyone can hit. Nothing verifies it, and no manual check can until that path
has a caller.

**X17 found the guard correct and the report inadequate.** The switch was
refused — `setWorkspaceDirectory` is never reached on that path, so the folder
genuinely did not change — but it was reported with `showTransientMessage`, a
four-second status line, while the loading overlay held the user's attention in
the middle of the viewport and they had just pressed OK on Preferences. The
observed result was an action that silently did not happen, which is worse than
one refused out loud. It now raises a modal box, the same correction X14 made
for the same reason.

The run also exposed that the guard had no way out. A slide open runs on a
detached thread with no timeout and no cancel: `loadingFinished` is emitted on
success (`ViewportWidget.cpp:2623`), on open failure (`:2487`) and from
`closeSlide()` (`:2356`), but a worker that hangs posts nothing, so
`slideLoadInFlight` would stay true for the rest of the session and no folder
change would ever be accepted again. Closing the slide clears it, and the menu
bar stays live under the overlay, so the message now names that route. **The
hang itself is a separate defect and is not fixed here** — an open that cannot
finish and cannot be cancelled is a problem this sub-project neither introduced
nor addresses.

**Both X16 and X17 need re-running** against a build carrying their fixes.
Neither can be covered by a test: the dialog needs a `QApplication` no suite
creates, and the in-flight guard needs a real async open.

## Compliance note

Annotations are clinical findings made on patient images, so this feature moves
and merges clinical data. Three properties follow from that, and each is a
design constraint above rather than a remark here:

- **Nothing is deleted.** The old workspace is never written to beyond the
  flush it already receives, and never deleted from. Every outcome leaves a
  complete set somewhere on disk.
- **No write is silent.** The overwrite names its target and count before it
  happens; the merge reports what it changed. Under EU MDR and the EU AI Act's
  high-risk obligations, a tool that silently alters a reader's findings is not
  defensible, and "it was reproducible" is not the same as "the user was told".
- **No guessing.** Where the new folder's file cannot be parsed, every write is
  withheld rather than attempted against a file whose contents are unknown.

Annotation files are Confidential under the organisation's data policy. Nothing
here changes where they live relative to the user's chosen folder, what they
contain, or the HIPAA Security Rule and GDPR posture of the existing store: the
workspace remains a local folder the user names, and no data leaves it.

## Deferred

- **Bulk migration** of a whole workspace, with per-file conflict reporting.
  The half-migrated store this leaves behind is real, and the dialog's wording
  is the mitigation, not a fix.
- A merge preview showing which annotations would be superseded before
  confirming. The receipt reports it afterwards; a preview would need the
  viewport to show both sets, which is a larger feature.
