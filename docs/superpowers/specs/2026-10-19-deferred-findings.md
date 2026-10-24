# Deferred minor findings — recovered index

Status: recovered 2026-10-19 from the session transcript, split into seven
batches and all seven worked by 2026-10-20: **117 of 117 triaged, 27 fixed, 23
stems unusable.** The full text of these findings is lost -- what is here is
each one's opening clause, enough to identify it and re-derive it against the
code, and nothing more.

The sweep's own claim of 117 triaged was one short until 2026-10-20: entry 66
carried no verdict at all, and no count caught it because the tally was kept by
hand. `awk '/^[0-9]+\. /{ if ($0 !~ /\*\*\[/) print }'` over this file is the
check that found it, and it is the one to re-run before claiming this index is
complete.

## Why this file exists, and what went wrong

Every round from 2026-10-07 onward kept a ledger at
`.superpowers/sdd/<round>/progress.md`, and each Minor finding the reviews
raised but did not act on was appended to it as a `minor (deferred):` line.
Fourteen rounds accumulated that way.

`.superpowers/` is in `.gitignore` (line 35) and was never tracked. The
directory is now empty. **The only durable copy of fourteen rounds of review
findings was a gitignored directory, and it is gone.** The transcript preserves
greps of those ledgers rather than the ledgers themselves, and an earlier
archaeology pass truncated its output at 105 characters — so every surviving
copy is cut at the same boundary, mid-sentence.

The lesson is the file you are reading: a finding nobody acted on still has to
live somewhere a `git clone` reaches. Future rounds should append to a tracked
followups document, not only to the round's ledger.

## How to use this

Each entry below is a **stem**, not a finding. Most name a file, a function or
a symbol, which is enough to find the code and judge the issue afresh — faster
than rediscovering it, slower than reading the original. Treat the wording as a
pointer, never as a specification, and never quote one as though it were the
whole finding.

They are sorted alphabetically because round attribution did not survive either.
The `#n` prefixes on the first ten are each round's own numbering, not a global
one, so they collide.

## Triage so far

**Twenty-seven of the 117 have been looked at, on 2026-10-19** -- fifteen in a
first pass, then twelve more as batch B1, whose results are under "The batches"
below. Of the first fifteen, **nine were already closed** by later rounds,
**four were live and are now fixed**, one is partly fixed and one is live and
accepted. The index is therefore mostly historical — but not entirely, and the
live ones were worth having.

Fixed on 2026-10-19 (entries 25, 34, 106, 107):

- **The list sorts folded no case while the filter did**, so every capitalised
  label collected above every lowercase one — `Zebra` before `apple` — in a
  list the user filters case-insensitively. The three sorts now fold, and still
  fall through to creation order so rows cannot swap between rebuilds.
- **Case folding was ASCII-only and locale-dependent** (`std::tolower`), so a
  category carrying any non-ASCII letter made a separate tally row for each
  case it was typed in — one clinical count split in two, each half correct
  about what it counted and neither correct about the category. `toLower` now
  uses `QString::toCaseFolded`, which is what Unicode defines for caseless
  matching.
- **`categoryTally` ran on every keystroke in the filter box.** It walks every
  annotation in the scene and its counts cannot move when the filter moves —
  that is the rule it is built around — so each keystroke paid for a strip
  identical to the one already on screen. `rebuild()` is split, and the filter
  and sort controls call `rebuildRows()`.
- **A tally key was lowercased before the checks that discard it.**

Still live and accepted: entry 68, `resizableBoxGeometry` computing
`boundingBox` before the test that may discard it — the box is what the
authority has to be *asked* with, which is the design the finding is weighing.

The nine found already closed are marked inline below. The spot-check sample:

- *"core::nearestBoxHandle treats a NaN distance as in[-range]"* — **closed.**
  `BoxResize.cpp:35-39` now writes the test as `!(distance <= tolerance)`
  precisely so NaN is refused, with the reasoning in a comment.
- *"window title still names the closed slide after File > Close"* — **closed**
  2026-10-19, along with two sibling defects group AC found; see the
  categorized-counting spec §7.5.
- *"CLAUDE.md claims the ui layer links slideio-viewer-app; it…"* — **appears
  closed.** `src/ui/CMakeLists.txt:53` links it and CLAUDE.md's target table
  says so too, so the two agree today.
- *"resizableBoxGeometry computes boundingBox before the box-shaped test"* —
  **still live.** `ViewportWidget.cpp:1641` computes it before the
  `withBoxReplaced` test on `:1645`, so the work is discarded for a polygon,
  point or arrow. Defensible rather than wrong: the box is what the authority
  has to be *asked with*, which is the design that finding is weighing.

Two others were re-checked earlier the same day and are recorded here so they
are not raised a third time: the **read-only rename asymmetry** is closed
(`MainWindow.cpp:2039-2042` carries the gate), and the **missing
`QSignalBlocker` in `AnnotationPropertiesPanel::setCategories`** cannot fire —
the combo connects `activated` (user-only in Qt) and `editingFinished`, neither
of which a programmatic `clear()`/`addItem()`/`setCurrentText()` emits. It is
latent, and becomes real the day someone connects `currentTextChanged`.

## The batches

The 117 are split into seven batches, each one area of the code, so a round can
be worked and built and reviewed as a unit. Every untriaged entry is in exactly
one batch. The numbers are the entry numbers in the list below.

| Batch | Area | Entries | Count | State |
|---|---|---|---|---|
| B1 | core geometry, measurement and numerics | 8, 14, 21, 29, 42, 44, 46, 57, 65, 70, 72, 104 | 12 | **done 2026-10-19** |
| B2 | AnnotationModel, undo/redo, app services | 3, 5, 6, 10, 16, 19 | 6 | **done 2026-10-20** |
| B3 | viewport interaction and drag state | 1, 7, 11, 33, 43, 58, 60, 62, 71, 76, 84, 88, 112 | 13 | **done 2026-10-20** |
| B4 | panels, categories, preferences | 13, 15, 18, 28, 32, 61, 64, 67, 69, 81, 83, 85, 86, 109 | 14 | **done 2026-10-20** (86 and 109 moved to B6) |
| B5 | persistence, paths, workspace, serialization | 12, 22, 23, 31, 36, 38, 39, 40, 59, 94, 96, 99, 100, 114, 117 | 15 | **done 2026-10-20** (4 stems unusable) |
| B6 | documentation and comments | 4, 17, 20, 35, 41, 48, 63, 74, 75, 78, 79, 80, 86, 90, 92, 95, 108, 109, 113 | 19 | **done 2026-10-20** (9 stems unusable) |
| B7 | test quality: weak, missing or vacuous cases | 26, 45, 47, 49, 50, 51, 52, 53, 54, 55, 56, 77, 82, 87, 89, 91, 93, 97, 98, 101, 102, 103, 105, 115 | 24 | **done 2026-10-20** (14 stems unusable) |

Already resolved outside the batches, in the first triage pass: 2, 9, 24, 25,
27, 30, 34, 37, 66, 68, 73, 106, 107, 110, 111, 116.

B6 and B7 are last on purpose. A comment that drifted and a test that is weaker
than it looks are both worth fixing, but neither can produce a wrong number on
a slide, and the stems in those two batches are the most truncated — several
will have to be re-derived from the code rather than read.

### B1 — core geometry, measurement and numerics (2026-10-19)

Twelve entries. Two were already closed by later work, four were live and are
fixed, and five are live-and-accepted or benign with the reasoning now written
down where the code is.

The one that mattered: **entry 72 was the core half of entry 106**, and fixing
106 alone on 2026-10-18 had left the two layers disagreeing. `ui::categoryTally`
folded case with `QString::toCaseFolded` while `core::normalizeCategoryList` and
`core::retagClassification` folded with `std::tolower`, which is ASCII-only. A
category typed once with an o-diaeresis and once in capitals was therefore **one
row of eight in the tally and two entries in the vocabulary, and renaming it
changed five of the eight** -- a count that is right about what it counted, and
a rename that silently misses a third of it. One fix cannot land on one side of
a layer boundary when both sides compare the same strings.

`core::caseFoldKey` is now the single fold, and ui calls it rather than Qt. It
covers ASCII, the Latin-1 Supplement, Latin Extended-A, Greek and Cyrillic,
applied to UTF-8 bytes; everything else passes through. Greek and Cyrillic were
added on 2026-10-20 after the narrowing was questioned -- see "Extending the
fold" below. `std::tolower` and `std::isspace` are gone from both files for the
same reason they were wrong: they read the global C locale, and under a
single-byte one they rewrite or trim the lead byte of a UTF-8 letter depending
on whether something else in the process had called `setlocale`.
The other three fixes:

- **Entry 104 was undefined behaviour, not just an ugly number.**
  `threeSignificantFigures` cast `std::floor(std::log10(magnitude))` to `int`,
  and for an infinite magnitude that cast is UB. `formatArea` reaches it: the
  conversion to square microns is `value * 1e12`, which overflows to infinity
  for any area above about 1.8e296 -- a finite double in the file, an infinite
  one here. A non-finite guard now runs first. The unit promotion then does the
  recovering on its own and the area still reports finitely in mm².
- **Entry 70**, the same site: the 64-byte buffer truncated rather than
  overran, so an extreme value produced a severed number that still looked like
  a measurement. The buffer is sized for `%.0f` of `DBL_MAX` and the decimal
  count is clamped.
- **Entry 42**: `measure()` built the outline for its self-intersection test and
  `measureKnown()` built an identical one immediately after. That path is
  exactly the shapes that are not `hasWellDefinedArea` -- the hand-traced
  polygons and freehand lines, which carry more vertices than anything else in
  the application. `measureOutline` now takes the one that was already built.
- **Entry 8**: `normalized()` was duplicated verbatim for rectangle and ellipse.
  Both, and the arrow's bounding box, call `normalizedCorners` now.

Accepted with reasons recorded rather than changed: 14 (the empty-polygon
sentinel, unreachable), 21 (the dangerous direction is already a compile
error), 29 (an epsilon would make collinearity depend on coordinate magnitude,
so panning a shape could change its self-intersection answer), 44 (the NaN path
returns the fixed arrowhead size, which is the benign answer), 65 (nothing
clamps an annotation to the slide extent, and nothing should -- a trace running
off the tissue edge is a real gesture).

Closed by earlier work: 46 (`inClosedRange` calls `std::isfinite`) and 57
(`ViewportController.cpp:99` clamps the scale straight after `fitToRect`).

### Extending the fold to Greek and Cyrillic (2026-10-20)

Not from the index. B1 made `core::caseFoldKey` cover ASCII, the Latin-1
Supplement and Latin Extended-A, which was wider than the `std::tolower` it
replaced and narrower than the `QString::toCaseFolded` the ui had carried for
one day. The project owner asked what the narrowing cost and whether it meant
only Latin was supported.

It did not. The fold is used **only** as a comparison key -- `==`, `find`, `<`,
and as a map key -- and never stored or displayed: `normalizeCategoryList`
keeps the trimmed original, `categoryTally` rows carry the spelling first seen,
and `retagClassification` writes the target as typed. A category in any script
was stored, shown, counted and renamed exactly as before. What a narrow fold
costs is only whether two differently-CASED spellings are judged the same
category.

And the boundary was never "Latin": it was **bicameral**. Chinese, Japanese,
Korean, Arabic, Hebrew, Thai and Devanagari have no case, so folding them to
themselves is what caseless matching means for them, in this implementation and
in Qt's. The scripts that actually lost something were Greek and Cyrillic.

Both are now folded, in their own blocks beside the Latin one:

- **Greek** is one contiguous 0x20 shift with U+03A2 punched out of it (that
  slot is unassigned, because lowercase sigma has two forms and uppercase has
  one). Two things are not shifts. The accented capitals sit BELOW the alphabet
  at U+0386..U+038F and fold up into letters scattered across U+03AC..U+03CE
  with no constant offset, so each is named. And **final sigma folds to
  ordinary sigma** -- a Greek word ending in sigma is spelled with a different
  letter from the same word with a suffix, so without that rule a category
  typed on its own and the same category typed inside a phrase would be two
  categories.
- **Cyrillic** is two shifts and four paired runs. The basic alphabet shifts by
  0x20; the letters Russian does not use and Ukrainian, Serbian, Macedonian and
  Belarusian do shift by 0x50, because Unicode placed those capitals before the
  alphabet and their lowercase forms after it. U+04C0 palochka is the outlier,
  folding to U+04CF at the far end of its run.

**The sweep is the part worth keeping.** Rather than a handful of examples, two
tests walk every codepoint from U+00C0 to U+04FF -- the exact span the decoder
claims -- and assert that folding is idempotent and that one codepoint maps to
one codepoint. Every range bound and parity rule in the three blocks is an
off-by-one away from mapping a lowercase letter onto a different letter, and
that shows up in the first sweep as a second fold moving something. The width
sweep failed on its first run at U+017F, which was the test being wrong rather
than the code: long s folds DOWN to one-byte ASCII s, the one mapping that
leaves the two-byte range.

Still not folded, and now pinned by a test so the boundary is on record:
Armenian and Georgian (which do have case), archaic and Coptic Greek, the Greek
mathematical symbol variants, and the Turkish dotless i -- whose correct fold
is locale-dependent and which Qt's default folding does not handle either.

### B2 — AnnotationModel, undo/redo, app services (2026-10-20)

Six entries, and **five were already closed** -- four of them by comments that
now stand exactly where the finding pointed, answering it in the code rather
than in a ledger. `setSelected` refuses an id the model does not hold and
`AnnotationEditCommand::apply` says so at the call site; `create()` rejects a
command recording neither state centrally, so a push site added later cannot
forget; `remove()` clears the selection only when the removed id is the
selected one; the bulk-retag signal reaches the properties panel's ordinary
`refresh`, not the reset path, so a field the user has focus in stays theirs;
and `~MainWindow` deletes the three Qt-owned widgets after `saveState()` while
every app and infra service is a `unique_ptr` in `Impl`.

Entry 16 was live and is fixed: `AnnotationEditing.cpp` used `std::nullopt`
while reaching `<optional>` only through `AnnotationEditCommand.h`.

**Found while working the batch, and not from the index: a shadowing hazard
introduced by the rebuild() split on 2026-10-19.** The connect block in
`AnnotationListPanel` had a local lambda *named* `rebuildRows` that calls
`rebuild()`, from before `rebuildRows()` existed as a member with deliberately
narrower behaviour. Every annotation signal still refreshed the tally, so
nothing was wrong at runtime -- but the next reader to decide whether a signal
should move a count would have read the name and got the opposite answer. It is
`rebuildAll` now. The split was right; leaving the old name beside it was not.

### B3 — viewport interaction and drag state (2026-10-20)

Thirteen entries, **eight already closed** -- and this is the batch where that
is most informative, because seven of the eight are closed by a comment
standing exactly where the finding pointed, naming the hazard it named.
`moveOriginal` is reset in the `else` branch; `updateCursor` has its
`MoveVertex` case; `endToolDrag` returns early unless the drag is the tool's;
`resetAnnotationState` clears every captured original; Escape falls through to
`QOpenGLWidget::keyPressEvent` with nothing in flight; the move applies
`core::translated`; `paintAnnotations` dispatches through `core::outline` and
`core::screenDecoration`. These were not fixed *because* of the index -- they
were fixed by later rounds that never struck them off it.

**Nothing here is reachable from a test**, which is why the two fixes are both
extractions rather than edits in place. The viewport cannot be constructed in
any suite; a rule left inside it is a rule nothing will ever check again.

- **Entries 62 and 76 are one defect: the preview and the commit each decided
  on their own what shape a box drag was going to be.** The painter chose
  between an ellipse path and `drawRect`; `commitDrawDrag` chose between
  `EllipseGeometry` and `RectangleGeometry` sixty lines away. Two places
  deciding that can disagree, and the way the user finds out is a dashed
  ellipse that releases into a rectangle. `ui::boxToolGeometry` is the one
  decision now, asked by both, and the preview paints it through the same
  `core::outline` and `screenPath` the committed shape is painted from -- so
  the preview is the shape by construction. The hand-rolled ellipse path loop
  (entry 62) went with it.
- **Entry 71 was real and small: holding Space and clicking cleared the
  selection.** Space is the user saying they are navigating, and a press under
  it becomes a pan; releasing without moving then ran the selection test
  anyway, in the one mode entered specifically to avoid touching the work.
  `ui::releaseSelectsAnnotation` refuses it. The care needed was in what NOT to
  exclude: a read-only slide converts a press on an annotation into a pan too,
  and clicking a mark to read its properties is the only way a reader has to
  select one. So the test is held Space, not "was this a pan".

Accepted: entry 7 (the preview omits the commit's `withMinimumExtent`, and
should -- the preview shows the gesture, and the one-pixel widen is what the
commit does to a gesture that collapsed on an axis; this is now stated where
the preview is painted) and entry 88 (the n-point tool hint is transient, but
`setActiveTool` has no early return, so re-pressing the tool key brings it
back; making it persist for the length of a trace is a design decision).

### B4 — panels, categories, preferences (2026-10-20)

Fourteen entries: five already closed, four fixed, three accepted, two moved
to B6 as duplication rather than defects, and **one left live because fixing
it is a decision, not a repair**.

**Entry 64 is the one worth reading.** `AnnotationModel::setGeometry` emitted
`annotationChanged`, and `AnnotationListPanel` rebuilds its whole tree on that
signal -- `clear()`, N `QTreeWidgetItem`s and a sort. A move-drag emits one per
mouse move. On a slide carrying a few hundred annotations, every frame of every
drag rebuilt several hundred list rows to produce rows identical to the ones
already on screen, and the stutter lands on the drag itself.

Nothing a row displays can change when a shape moves: a row shows a label, a
type and a classification, and `core::typeOf` is derived from the geometry's
variant alternative, which a translate or a resize keeps. So `setGeometry` now
emits `annotationGeometryChanged`, and the list panel is the one listener that
does not connect it. The viewport does (it repaints), the properties panel does
(it measures perimeter and area from the geometry), and
`AnnotationPersistenceService` does (a move is an edit and the file is stale
until it is written). Making each of the four state its own answer is the
point; a signal everyone connects by default is the thing that was wrong.

This is the same defect as entry 25, one layer up: work whose result is
guaranteed to equal what is already displayed.

The other three fixes:

- **Entry 85**: the properties panel's empty state cleared the measurement
  values but not their captions, which are set from the last annotation shown.
  Deselecting a polygon left an empty "Area" row and a caption still reading
  "Perimeter" -- a measurement frame for a shape that is no longer selected.
- **Entry 61**: `ui::categoryTally` kept `rows` and `keys` as parallel vectors,
  maintained by hand in four places and then deliberately broken in a fifth --
  the synthetic Unclassified row was pushed to `rows` and not to `keys`,
  harmless only because nothing looked anything up afterwards. It is a map from
  key to row index now, which cannot be one entry out of step, and the linear
  scan per annotation goes with it.
- **Entry 18**: `widget()->layout()` dereferenced without a null check.

**Entry 67 is live and is left live on purpose.** Renaming a category with no
slide open reports "These annotations are open read-only and cannot be
changed", because the gate asks `annotationPersistence->isActive()`, which is
false for a read-only slide and for no slide at all. The message names a cause
that is not the cause, which this project treats as worse than naming none. But
the fix is not a better message: the category list is a global preference
(CLAUDE.md), so a vocabulary rename with nothing open is arguably valid and
should simply retag nothing. That is the project owner's call, not a repair to
make silently.

Accepted: 13 (the Preferences write is idempotent -- the store normalises on
both sides), 81 (the tally context menu resolving by `itemAt` is the coupling
AC29 was struck over, already recorded), 83 (a configured category list is a
handful of entries; the same shape in `categoryTally`, which walks every
annotation, is the one that earned a map).

### B5 — persistence, paths, workspace, serialization (2026-10-20)

Fifteen entries: three already closed, four fixed, four accepted, and **four
whose stems are too truncated to act on**. This is the first batch where that
last group appears, and it will be most of B6 and B7. An entry like *"bad_alloc
from the outer catch is reported as"* names neither the site nor what it is
reported as; guessing at the rest of the sentence and fixing the guess is worse
than leaving it indexed. Those four are marked and left.

The two that mattered are both about telling the user something true:

- **Entry 38.** The workspace Cancel path captured `workspaceDirectory()`,
  which RESOLVES an empty setting to the default. Restoring that on Cancel
  wrote the resolved path back as an explicit setting -- so a user who had
  never chosen a workspace, opened the dialog and then cancelled, came out with
  their workspace pinned to today's Documents folder. Move Documents later and
  the viewer keeps writing annotations to the old location, with nothing having
  announced the change. `configuredWorkspaceDirectory()` returns what is
  actually stored, and Cancel restores that.
- **Entry 39.** `result.asked` was set from `options.ask` alone, before the
  resolver was checked. With no resolver, `run()` chooses `LoadNew` itself --
  and `MainWindow` reads `asked` to decide how to report what happened to the
  annotations. It would have reported a choice the user was never offered. It
  is now `options.ask && m_resolver`, with a test.

Also fixed: **entry 36**, where `defaultAnnotationWorkspaceDirectory()` built
`documents + "/" + applicationName()` without checking that Documents resolved.
On a Linux account with no xdg-user-dirs configuration it comes back empty and
that produced `"/slideio-viewer"` -- a directory at the filesystem root, which
is unwritable on any normal account and the wrong place for clinical findings
on one where it is not. It falls back to `AppLocalDataLocation` now, where the
log file already lives.

And **entry 100**, which was three copies of one rule: the condition deciding
whether a supplied colour profile was actually USED stood verbatim in both of
`SlideIOAdapter`'s open paths, each with a comment saying it "mirrors the
condition inside buildManagedScene exactly". Nothing kept them in step, and
what drifts is what the properties panel tells the user they are looking at.
`applySuppliedProfileOrigin` is the one mirror now.

Entry 99 is worth recording as closed rather than merely silent: the serializer
refuses an empty or duplicate id before writing anything
(`AnnotationSerialization.cpp:662`), so the "never write a file we cannot read
back" invariant holds against that case.

Accepted: 12 (a hand-typed `fillOpacity` round-trips, because nlohmann writes
the shortest round-tripping form of the float), 23 (`beginSlide` reads the
annotation file on the UI thread -- real, but moving annotation I/O off it is
architecture, and the read happens inside a slide open that already shows a
loading overlay), 40 (Cancel must re-enter `beginSlide`, because the repoint
just changed which file is authoritative), 117 (annotation filenames are
`<slideId>.s<N>.annotations.json` and a slideId is FNV-1a hex, so no Windows
reserved device name can be produced).

### B6 — documentation and comments (2026-10-20)

Nineteen entries including the two moved from B4. **One fix, three closed, and
the rest either not findable or not findings.** This is the batch the
truncation hurt most, and the shape of the damage is worth naming: five stems
are cut at an opening quote -- *"the Preferences tooltip says "*, *"the comment
at ViewportWidget.cpp:4201 ("* -- so the finding was entirely in the quoted
text. Two more are about round reports under `.superpowers/sdd/`, which no
longer exists. Two record that there was nothing to do.

The one fix, entry 113, was worth having: `Viewport::fitToRect` documented
neither its `[0, 0.45]` clamp on `marginFraction` nor the more surprising fact
that it applies **no zoom limit at all**. A sub-pixel target box yields an
enormous scale, and `ui::ViewportController` clamps it immediately afterwards
because the limits are a ui concern core cannot see. That is now in the header,
which is where someone adding a second caller would look. Entry 57, in B1, was
the same fact arriving from the other direction.

Closed: 35 (`Iso8601.h` names the 1970 floor as well as the 9999 ceiling), 108
(no trailing whitespace remains), 4 (the finding says the code is right and the
plan document is not, and CLAUDE.md already settles that the code is the truth).

Accepted: 20 -- `AnnotationShapeName.cpp` orders its three dispatch sites by
variant alternative rather than alphabetically, and consistently. That is more
useful than alphabetical here, because it is the order the compiler enumerates
them in when a new shape fires every site.

Entries 86 and 109, moved here from B4, are duplication with no behavioural
difference: `onSetSlideColorProfile`'s file-pick-and-validate block, and the
`QStringList`/`vector<string>` conversions the category store does on both
sides of its own boundary and `MainWindow` once more. Both are real and neither
can produce a wrong result; they are left as indexed, not fixed, because this
round had no reason to be in those files.

### B7 — test quality (2026-10-20)

Twenty-four entries: **four closed, five fixed, one accepted, and fourteen
whose stems are unusable.** The ratio is not an accident. A test-quality
finding is almost entirely its reasoning -- *"the cross-session persistence
test would pass with"* is cut at the exact word where the weakness was named,
and *"the first Annotation test is also weak against a"* likewise. A
correctness finding usually names a file or a symbol in its first clause and
survives truncation; these did not.

Where a stem was guessable I said so and still did not act -- entry 89
(asserting two of four corners would pass for a winding-order change), 101 (a
tie-break test passes vacuously when the input is already ordered), 102 (an
equality that holds because neither side was ever set). Those are plausible
reconstructions, not the findings, and writing a test against a guess would put
a false record in the suite.

Five were real gaps and are now covered:

- **Entries 52 and 56** are the ones worth having, because they guard the rule
  B1 changed. `normalizeCategoryList` had trimming and case folding covered
  separately, and neither test would have noticed a key that applied one and
  not the other -- which is precisely the defect entries 72 and 106 were.
  `ui::categoryTally` had no test at all against a configured list carrying
  padded or case-variant entries, nor against a non-ASCII case pair. Both now
  do, and the non-ASCII case is the one that read eight while a rename of it
  changed five.
- **Entry 77**: the `[0, 0.45]` margin clamp had no test. Two now: an over-asked
  margin fits identically to 0.45, and a negative one identically to 0.
- **Entry 103**: the zero-translation identity test asserted x and y and not
  width and height, so it would have passed for a translate that moved one
  corner and left the other -- the exact bug an identity test exists to catch.

Closed: 51 (`computeSlideId` IS pinned against a hardcoded hex string, which is
what keeps stored annotation files addressable), 54, 105, and 93 in part.

Accepted: 45, "NaN geometry inputs untested". A test here would pin an
accident -- `std::min(NaN, x)` returning `x` is a consequence of how the
comparison fails, not a decision. The guarantee that matters is the
serializer's `isFinitePoint`, which refuses NaN on read and on write, and that
is tested.

### Entry 67, and the one entry the sweep missed (2026-10-20)

Entry 67 was left live by the sweep because it needed a decision rather than a
repair. The gate on Rename... asked
`annotationPersistence->isActive()`, which is false for a slide whose
annotation file could not be parsed AND for no slide at all, so renaming a
category on an empty viewer answered "These annotations are open read-only and
cannot be changed" -- naming a slide and annotations the user did not have.

The decision: **the two states are not the same refusal, and one of them is not
a refusal at all.** The category list is a global preference, deliberately off
the per-slide undo stack, so with nothing open a rename edits a preference and
has nothing to retag. Remove from list, directly beside it in the same context
menu, has never been gated at all -- that asymmetry is what showed the gate was
in the wrong place rather than merely wrongly worded.

With a slide open that refuses the retag, the refusal stands and is now
deliberate (entry 66, which the sweep had left with no verdict at all).
Renaming the vocabulary half alone there would leave the annotations on screen
carrying the old word: the tally would show the new name at zero beside an
unconfigured row holding the real count, and the list being global, that split
would follow the user to every other slide.

`ui::categoryRenameScope(slideOpen, annotationsEditable)` is the rule, returning
`Refused`, `VocabularyOnly` or `VocabularyAndAnnotations` -- in ui policy rather
than in the connect lambda, for the usual reason: no test binary creates a
`QApplication`, so a rule inside `MainWindow` is a rule nothing will ever check.
Four tests. The undo-stack guard moved with it, since only the retag needs a
stack. The trailing message splits too: "No slide is open; the list entry was
renamed" rather than the per-slide wording.

The rule is tested; the wiring that asks it is not, and cannot be. So this was
run by hand against the built viewer on 2026-10-20 and passed, and it went into
group AC of the counting spec as **AC31** rather than being reported once and
forgotten -- a manual check that lives only in a session is a check nobody will
ever run again.

Entry 66 also says something about the sweep itself. The index claimed 117 of
117 triaged while one entry carried no verdict, because the tally was kept by
hand and nothing checked it. The check is one line --
`awk '/^[0-9]+\. /{ if ($0 !~ /\*\*\[/) print }'` -- and it belongs at the top
of this file, not in a session that has already ended.

## The findings

1. #10 moveOriginalGeometry's unguarded `if (picked)` could **[CLOSED - the else branch resets moveOriginal, ViewportWidget.cpp:4675, so a capture from an earlier drag cannot be restored onto this annotation]**
2. #11 the front-trim loop erases one element at a time, O(n) **[CLOSED - UndoRedoStack.cpp:39-46 is one range erase, with the reasoning in a comment]**
3. #4 setSelected() unconditional after an insert() that can drop **[CLOSED - AnnotationModel::setSelected refuses an id the model does not hold, and AnnotationEditCommand::apply says so at the call: a dropped insert leaves the previous selection standing]**
4. #4 Shift+Ctrl squares the box (code correct, plan **[CLOSED by its own wording - the finding says the code is correct and the plan document is not; `documents/` is the plan, and CLAUDE.md already says the code is the truth where they disagree]**
5. #5 an invalid command is rejected by convention at the call **[CLOSED - AnnotationEditCommand::create rejects a command recording neither state, centrally, so a push site added later cannot forget]**
6. #6 ~MainWindow nulls the persistence pointer but not **[CLOSED - the three Qt-owned widgets are deleted and nulled explicitly, after saveState; every app and infra service is a unique_ptr in Impl and dies with it]**
7. #6 preview omits withMinimumExtent that the commit **[ACCEPTED 2026-10-20 - and now stated where the preview is painted: the preview shows the gesture, and the one-screen-pixel widen is what the commit does to a gesture collapsed on one axis]**
8. #7 normalized(EllipseGeometry) duplicates the **[FIXED 2026-10-19 - normalizedCorners(PointF, PointF) is shared by rectangle, ellipse and the arrow box]**
9. #8 the pi literal is repeated three times in **[CLOSED - single constexpr kPi at AnnotationGeometry.cpp:15]**
10. #8 undoing a create clears an unrelated selection -- exactly **[CLOSED - AnnotationModel::remove clears the selection only when the removed id IS the selected one, AnnotationModel.cpp:140]**
11. a 1-pixel jitter counts as a pan and suppresses **[CLOSED - the selection test is the press-to-release distance, not whether a move event arrived; now in ui::releaseSelectsAnnotation]**
12. a hand-edited file carrying fillOpacity 0.333 quantises and **[ACCEPTED 2026-10-20 - fillOpacity is a float and nlohmann writes the shortest round-tripping form, so a value typed by hand comes back as itself]**
13. accepting Preferences writes the category store even when **[ACCEPTED 2026-10-20 - the store normalises on write and on read, so the rewrite is idempotent, and re-feeding the panels is how a normalisation becomes visible]**
14. an empty polygon's boundingBox is {0,0,0,0} — a real rect at **[ACCEPTED 2026-10-19 - documented as a sentinel at AnnotationGeometry.cpp:76; no reachable path builds an empty polygon, so std::optional would make every caller handle a case none can meet]**
15. an empty/whitespace-only `to` retags to an empty **[CLOSED - core::retagClassification refuses an empty trimmed target and returns no changes, so no undo step is recorded either]**
16. AnnotationEditing.cpp uses std::nullopt with only an **[FIXED 2026-10-20 - <optional> is included directly; it had been reaching it through AnnotationEditCommand.h]**
17. AnnotationGeometry.h header comment **[STEM TOO TRUNCATED - the clause stops before saying what about it. Nothing findable]**
18. AnnotationPropertiesPanel.cpp:304 dereferences widget()->layout() **[FIXED 2026-10-20 - null-checked; qobject_cast already tolerates a null argument, so the guard costs one ternary]**
19. annotationsBulkChanged -> refreshAfterReset forces stored **[CLOSED - AnnotationPropertiesPanel.cpp:232 connects the bulk signal to refresh, not refreshAfterReset, so a field the user has focus in stays theirs]**
20. AnnotationShapeName.cpp breaks alphabetical order in **[ACCEPTED 2026-10-20 - all three sites follow the variant alternative order (Rectangle, Ellipse, Polygon, Point, Arrow) consistently, which is more useful than alphabetical: it is the order the compiler enumerates them in]**
21. AnnotationType and the geometry variant are independent **[ACCEPTED 2026-10-19 - the dangerous direction is a compile error: a new variant alternative with no typeOfShape overload will not build. The reverse leaves an unreachable enumerator]**
22. bad_alloc from the outer catch is reported as **[STEM TOO TRUNCATED - the clause names neither the site nor what it is reported as. Re-derive if the load/save error paths are touched]**
23. beginSlide's file read is on the UI thread; a **[LIVE, ACCEPTED 2026-10-20 - confirmed at AnnotationPersistenceService.cpp:94. Moving annotation I/O off the UI thread is architecture, not a repair; the read happens inside a slide open that already shows a loading overlay, and these files are small]**
24. categories() does not normalise on read, so a hand-edited **[CLOSED - categories() calls core::normalizeCategoryList on read, QSettingsAnnotationCategoryStore.cpp:44]**
25. categoryTally recomputes on every filter keystroke though **[FIXED 2026-10-19 - rebuild() split; the filter and sort combos call rebuildRows(), which does not touch the tally]**
26. ceiling test 2 is vacuous on MSVC and libc++ — its **[STEM TOO TRUNCATED - "its" is where the reason was. The Iso8601 ceiling is covered by name; whether one case is vacuous on a given standard library needs the original]**
27. CLAUDE.md claims the ui layer links slideio-viewer-app; it **[appears CLOSED 2026-10-19 - CMake and CLAUDE.md agree today]**
28. clearing the user name (setUserName( **[CLOSED - setUserName removes the key rather than storing an empty string, so hasUserName() answers correctly afterwards]**
29. collinearity is exact (d == 0.0), no epsilon. Correct and **[ACCEPTED 2026-10-19 - an epsilon would make "collinear" depend on coordinate magnitude, so a shape would change its self-intersection answer by being panned]**
30. core::nearestBoxHandle treats a NaN distance as in **[CLOSED - BoxResize.cpp:35-39 refuses NaN deliberately]**
31. currentSlideId() recomputes the content-size walk that **[CLOSED - MainWindow::currentSlideId delegates to the viewport, which kept what the background open worker computed; the comment names this finding]**
32. displacedProfileNote takes a const SlideInfo&, so **[CLOSED - it takes a bool and a string now, in SlidePropertiesPanel.h, so it is a pure function a test can reach]**
33. Escape is accepted unconditionally, blocking any parent **[CLOSED - with nothing in flight it falls through to QOpenGLWidget::keyPressEvent, ViewportWidget.cpp:5266]**
34. filter folds case but the Label/Classification/Type sorts **[FIXED 2026-10-19 - the three sorts fold case, tests in AnnotationListPolicyTest.cpp]**
35. formatIso8601Utc's header comment names only the 9999 **[CLOSED - Iso8601.h:11-14 names the 1970 floor and the 9999 ceiling, and says why both cost nothing]**
36. if DocumentsLocation resolves empty (rare Linux), the **[FIXED 2026-10-20 - it produced "/slideio-viewer" at the filesystem root; falls back to AppLocalDataLocation, where the log already lives]**
37. item->setData(0, Qt::UserRole, row.configured) is a dead **[CLOSED - no such call remains]**
38. M3 — `previousWorkspace` captures the resolved path, so **[FIXED 2026-10-20 - captured raw now, through the new configuredWorkspaceDirectory(). Restoring the resolved path on Cancel pinned the workspace at today's Documents folder]**
39. M5 — `result.asked` is set before the resolver check, **[FIXED 2026-10-20 - it is options.ask AND a resolver existing; `asked` means the user was asked, and MainWindow reports on it]**
40. M6 — Cancel re-enters beginSlide, which re-runs the **[ACCEPTED 2026-10-20 - it has to: m_repointTo(oldRoot) just changed which file is authoritative, so the model must be reloaded from the restored root]**
41. MainWindow.cpp now 1191 lines; mirrors its existing pattern **[STALE - the file is past 2400 lines now. Its size is a standing fact, not a finding to close]**
42. measure() calls outline(geometry) and then measureKnown calls **[FIXED 2026-10-19 - measureOutline takes the outline measure() already built; the double build fell on freehand traces, the shapes with the most vertices]**
43. move rebuilds geometry as RectangleGeometry from the **[CLOSED - the move applies core::translated to the captured geometry, ViewportWidget.cpp:4925]**
44. NaN behaviour is benign but accidental. std::min(NaN, 5.0f) **[ACCEPTED 2026-10-19 - benign and now stated: AnnotationDecoration.cpp returns the fixed head size, so the glyph draws nominally rather than vanishing]**
45. NaN geometry inputs untested **[ACCEPTED 2026-10-20 - a test here would pin an accident. The guarantee that matters is the serializer's: isFinitePoint refuses NaN on read and write, which is tested]**
46. NaN in lineWidth/fillOpacity always compares unequal, so it **[CLOSED - inClosedRange calls std::isfinite first, AnnotationSerialization.cpp:103, so NaN is refused on both read and write]**
47. new #includes/usings sit mid-file in the test, against the **[STEM TOO TRUNCATED to locate - it names no file, and several test files have had their include blocks rewritten since]**
48. new isUnclassifiedCategory declaration was inserted between **[MOOT 2026-10-20 - the header was reordered when caseFoldKey landed; it now reads predicates then operations]**
49. new test names start lowercase; the file's existing cases start **[NOT ACTIONABLE as an entry - it names no file. Treated as a standing rule: match the surrounding file's case]**
50. no exact-boundary test — ceiling accepted vs ceiling+1 **[STEM TOO TRUNCATED to place with confidence - probably Iso8601, where the ceiling is covered by name; re-derive if that area is touched]**
51. no golden-vector test pins the exact FNV-1a-over-separated-string **[CLOSED - SlideIdTest.cpp:133 pins computeSlideId against a hardcoded hex string, which is what keeps stored annotation files addressable]**
52. no test for a duplicate matching only after trim AND case **[FIXED 2026-10-20 - two tests added; trimming and folding were each covered alone, which would not notice a key that applied one and not the other]**
53. no test has two annotations with differing modifiedAt. **[STEM TOO TRUNCATED to place - it names no test file, and modifiedAt ordering matters in several]**
54. no test pins that a stored classification with surrounding **[CLOSED - AnnotationCategoriesTest.cpp:179, "retagClassification matches a stored classification with surrounding whitespace"]**
55. no test that an unset store leaves the file unwritten, nor **[STEM TOO TRUNCATED - cut at "nor", where the second half of the gap was]**
56. no tests for case-duplicate or whitespace-padded CONFIGURED **[FIXED 2026-10-20 - categoryTally is now tested against a configured list carrying both, and against a non-ASCII case pair]**
57. no upper bound on scale — a sub-pixel bounding box yields a **[CLOSED - ViewportController.cpp:99 clamps to kMinScale..kMaxScale right after fitToRect, with a comment saying core knows nothing of the limits]**
58. nothing resets dragOwner when a slide closes mid-drag; **[CLOSED - resetAnnotationState clears dragOwner, dragButton and every captured original, then calls updateCursor]**
59. openAuxImage has no live caller today (associated images go via **[STEM TOO TRUNCATED to act on - openAuxImageSync does have callers; whether the public ViewportWidget::openAuxImage is dead needs re-deriving]**
60. paintAnnotations has no per-type geometry dispatch; needed **[CLOSED - it paints from core::outline and core::screenDecoration, so the dispatch is in core where the variant is]**
61. parallel `rows`/`keys` vectors could desynchronise if a **[FIXED 2026-10-20 - an unordered_map from key to row index; the synthetic Unclassified row had already broken the invariant on purpose]**
62. preview rectangle repeats the screenRect conversion. **[FIXED 2026-10-20 - the preview draws screenPath(core::outline(...)); the hand-rolled ellipse path loop is gone]**
63. ragged comment line-wrapping left by the edits **[NOT ACTIONABLE as an entry - it names no site. Treated as a standing rule instead: reflow what an edit leaves ragged, which these batches have done as they went]**
64. rebuild() runs on every annotationChanged. If a move-drag **[FIXED 2026-10-20 - setGeometry emits a separate annotationGeometryChanged; the list panel does not connect it, the viewport, properties panel and persistence service do]**
65. rectangles are not clamped to the slide extent. Recorded as **[LIVE, ACCEPTED 2026-10-19 - no clamp exists. A trace running off the tissue edge is a real gesture and the slide extent is not a clinical boundary; clamping would move a vertex the user placed]**
66. rename is refused entirely on a read-only slide including the **[LIVE, ACCEPTED 2026-10-20 - and the one entry of the 117 that the sweep left with no annotation at all, found by grepping for lines carrying no verdict. The refusal is now deliberate and named: ui::CategoryRenameScope::Refused. Renaming the vocabulary half alone on a read-only slide would leave the annotations on screen carrying the old word, so the tally would show the new name at zero beside an unconfigured row holding the real count - and the list is a GLOBAL preference, so that split would follow the user to every other slide. The vocabulary-only rename is right with NOTHING open (67), not with a slide open that refuses the retag]**
67. rename with no slide open reports **[FIXED 2026-10-20 - ui::categoryRenameScope(slideOpen, annotationsEditable) decides, and MainWindow asks it instead of asking isActive() alone. With no slide open the rename goes through as a vocabulary-only edit, retagging nothing and recording no undo step, which is what Remove from list - beside it in the same menu, never gated - has always done. The trailing message splits too: "No slide is open; the list entry was renamed" rather than naming a slide the user does not have. Four tests, and OBSERVED in the running application 2026-10-20 by the project owner -- group AC31 in the counting spec, since no suite can reach the wiring that asks the rule]**
68. resizableBoxGeometry computes boundingBox before the **[STILL LIVE - ViewportWidget.cpp:1641 vs :1645]**
69. setAnnotationModel(nullptr) on an already-null model returns **[CLOSED - benign: both are null, so there is nothing to disconnect and nothing to clear; the panel is already showing nothing]**
70. snprintf buffer truncation rather than overrun for extreme **[FIXED 2026-10-19 - kNumberBufferSize is 352, enough for %.0f of DBL_MAX; see 104]**
71. Space held + left click with no movement clears the **[FIXED 2026-10-20 - ui::releaseSelectsAnnotation refuses a click made under held Space; a pan the user did not ask for, such as a read-only slide's, still selects]**
72. std::tolower/std::isspace are locale-dependent where the **[FIXED 2026-10-19 - core::caseFoldKey replaces both, and ui now calls it too; this was the core half of 106]**
73. tally not cleared when the model goes null (unreachable today). **[CLOSED - rebuildTally() clears before the null check]**
74. task-2-report.md per-case assertion breakdown lists cases **[NOT A CODE FINDING - it is about a round report under .superpowers/sdd/, which is gone]**
75. task-3-report.md file-size table reports 66/65/108 lines **[NOT A CODE FINDING - same: a round report under .superpowers/sdd/, which is gone]**
76. that condition is now DUPLICATED between the preview **[FIXED 2026-10-20 - ui::boxToolGeometry is the one place deciding whether a box drag is an ellipse or a rectangle; preview and commit both ask it]**
77. the 0.45 margin clamp has no test of its own. **[FIXED 2026-10-20 - two tests: an over-asked margin fits identically to 0.45, and a negative one identically to 0]**
78. the 720->900px widen + resizeColumnsToContents judged JUSTIFIED, **[NO ACTION - the finding records a judgement that the change was justified]**
79. the ambiguity fix was correct and test-only; no production change **[NO ACTION - the finding records that there was nothing to do]**
80. the comment at ViewportWidget.cpp:4201 ( **[STEM TOO TRUNCATED - cut at the opening quote, and the line has moved several times since. Nothing findable]**
81. the context-menu handler is correct only because nothing reads **[ACCEPTED - this is the coupling AC29 was struck over: the tally resolves its row by itemAt(at) and carries NoSelection + NoFocus deliberately. Recorded in the categorized-counting spec]**
82. the cross-session persistence test would pass with **[STEM TOO TRUNCATED - "would pass with" is exactly where the weakness was named]**
83. the dedup `seen` vector is searched linearly (O(n^2)); fine **[ACCEPTED 2026-10-20 - a configured category list is a handful of entries. The same shape in ui::categoryTally, which walks every annotation, is the one that was worth a map; entry 61]**
84. the dragOwner reset in commitPendingShape is unconditional, so **[CLOSED - endToolDrag returns early unless dragOwner == Tool, so a pan stays alive across a commit, a cancel or a tool change]**
85. the empty state clears the measurement VALUES but does not **[FIXED 2026-10-20 - the captions too: deselecting a polygon left an empty "Area" row and a caption reading "Perimeter"]**
86. the file-pick+validate block in onSetSlideColorProfile duplicates **[DEFERRED to B6 - duplication, not a defect; no behaviour differs between the two blocks today]**
87. the first Annotation test is also weak against a **[STEM TOO TRUNCATED - "weak against a" is where the finding was]**
88. the hint is transient and set on activeToolChanged, so a **[LIVE, ACCEPTED 2026-10-20 - setActiveTool has no early return, so re-pressing the tool key re-shows it. Making it persist for the length of a trace is a design decision, not a defect fix]**
89. the inverted-rectangle outline test asserts corners 0 and 2 **[STEM TOO TRUNCATED to act on, though the shape is guessable: asserting two of four corners would pass for a winding order change. Re-derive if outline() is touched]**
90. the movePressSlide comment's **[STEM TOO TRUNCATED - cut at the opening quote]**
91. the negative test covers rectangle and arrow only; an **[STEM TOO TRUNCATED to locate the test, though the gap is named: a third type is uncovered]**
92. the new include is one position out of alphabetical order in a **[STEM TOO TRUNCATED to locate - it names neither the include nor the file, and the include blocks have been rewritten since in several of these files]**
93. the normalisation test covers one case; core's tests cover **[PARTLY ADDRESSED 2026-10-20 - core's normalisation now has combined trim-and-fold coverage, and ui's categoryTally has its own; entries 52 and 56]**
94. the pacing fix (m_lastMutation push-forward) is **[STEM TOO TRUNCATED - the clause stops before the judgement. The push-forward is at AnnotationPersistenceService.cpp:193; re-derive if autosave pacing is touched]**
95. the Preferences tooltip says **[STEM TOO TRUNCATED - cut at the opening quote]**
96. the quit cannot be aborted — see R27. **[CLOSED by reference - the finding defers to a recorded decision, R27, rather than raising anything itself]**
97. the refused-save test does not pin the serialize-first **[STEM TOO TRUNCATED - "serialize-first" names an ordering the rest of the sentence described]**
98. the second new test (getter vs static resolver) is **[STEM TOO TRUNCATED - cut before the judgement]**
99. the serializer will write an empty annotation id that **[CLOSED - AnnotationSerialization.cpp:662 refuses an empty or duplicate id before writing anything, so the "never write a file we cannot read back" invariant holds]**
100. the suppliedWasUsed block is duplicated verbatim across both **[FIXED 2026-10-20 - applySuppliedProfileOrigin is the one mirror of buildManagedScene's rule; there were three copies and nothing keeping them in step]**
101. the tie-break tests depend on std::sort actually reordering **[STEM TOO TRUNCATED to act on, though the hazard is guessable: a tie-break test passes vacuously when the input is already ordered. Worth re-deriving if the list sorts change]**
102. the unchanged-edit test's modifiedAt equality could pass **[STEM TOO TRUNCATED to act on, though the hazard is guessable: an equality that passes because both sides were never set]**
103. the zero-translation identity test asserts x and y but not **[FIXED 2026-10-20 - width and height too; the origin alone would pass for a translate that moved one corner and left the other]**
104. threeSignificantFigures can ask for ~302 decimals at a **[FIXED 2026-10-19 - decimals clamped to kMaxDecimals, and a non-finite guard added: log10(inf) cast to int was undefined behaviour]**
105. tolerance on a degenerate rectangle untested **[CLOSED - AnnotationGeometryTest.cpp:112, "hitTest respects tolerance on a degenerate rectangle"]**
106. toLower is ASCII-only, so non-ASCII case variants split rows **[FIXED 2026-10-19 - toLower uses QString::toCaseFolded; test uses an O-diaeresis pair]**
107. toLower(clean) computed twice per non-empty classification. **[FIXED 2026-10-19 - the key is computed after the refusals that discard it]**
108. trailing whitespace on the loadFailure line in load(). **[CLOSED - no trailing whitespace remains in JsonAnnotationRepository.cpp]**
109. two hand-written QStringList<->vector<string> conversion **[DEFERRED to B6 - duplication, not a defect; the store converts on both sides of its own boundary and MainWindow once more]**
110. ui uses QString case-insensitive comparison where core uses **[PARTLY FIXED 2026-10-19 - ui now folds case the Unicode way; core still folds ASCII, which is sufficient for its ASCII-only comparand]**
111. unused <QSettings> include in the test file. **[CLOSED - QSettings is used at QSettingsAnnotationCategoryStoreTest.cpp:84]**
112. updateCursor has no DragOwner::MoveVertex case, so a **[CLOSED - the case exists at ViewportWidget.cpp:3862 with the reasoning beside it]**
113. Viewport.h's doc comment does not mention the [0, 0.45] **[FIXED 2026-10-20 - fitToRect now documents the margin clamp and, with it, that the function applies no zoom limit of its own; that is ui::ViewportController's job]**
114. while the service is null the two emissions are **[STEM TOO TRUNCATED - "the two emissions" names nothing findable. Re-derive if the persistence service's signal wiring is touched]**
115. whitespace asymmetry in the (none) test — **[STEM TOO TRUNCATED - cut at the dash]**
116. window title still names the closed slide after File > Close **[CLOSED 2026-10-19 - commit 3baecc2]**
117. Windows reserved device names (CON, NUL, AUX, PRN, **[ACCEPTED 2026-10-20 - annotation filenames are "<slideId>.s<N>.annotations.json" and a slideId is FNV-1a hex, so no reserved name can be produced. A user-chosen workspace directory is the OS's to refuse]**


## Appended after the recovery

Findings raised after 2026-10-20 and deferred, in full rather than as stems.

118. GeoJSON export (2026-10-21): a NaN or infinite `lineWidth` or `fillOpacity`, or an infinite pixel size, is written as `null` rather than refused **[FIXED 2026-10-23 - the export refuses a style the serializer would refuse, naming the annotation; a non-finite pixel size is left out like an unknown one]**
119. GeoJSON export: the classification is written untrimmed while `categoryTally` trims it, so " Tumor " becomes its own QuPath class **[FIXED 2026-10-23 - the export writes the classification ASCII-trimmed, as categoryTally counts it]**
120. GeoJSON export: the spec names the action "Export Annotations..." and the menu says "Export Annotations as GeoJSON..." **[FIXED 2026-10-23 - the export spec now names the action as the menu does]**
121. GeoJSON export: invalid UTF-8 is tested only in notes, and out-of-bounds coordinates only on a point **[MOVED 2026-10-23 to docs/technical-debt.md as TD-1]**
122. GeoJSON import (2026-10-22): a 206 MB, 60 000-cell QuPath detection export is skipped correctly but takes about 10 s on the UI thread, with no wait cursor; nlohmann's parse and destruction are most of it **[MOVED 2026-10-23 to docs/technical-debt.md as TD-2, then FIXED 2026-10-23 - read on a worker thread behind a cancellable dialog, non-annotation features dropped during the parse: 13.8 s and 991 MB down to 6.0 s and 297 MB]**
123. GeoJSON import: the scene-mismatch warning chains `.arg(scene).arg(slide).arg(openScene)`, so a `%3` in the file's slide name is substituted by the last call **[FIXED 2026-10-23 - one multi-argument arg(), so the file's slide name is never substituted into]**
124. GeoJSON import: a zero-extent rectangle re-imports as a 4-vertex polygon, because `isAxisAlignedBox` requires distinct corners and the export writes coincident ones **[FIXED 2026-10-23 - a ring the file calls a rectangle is accepted with coincident corners; a foreign one still needs distinct corners]**
125. GeoJSON import: an exported ellipse's box comes back within a few ulps, not bit-exact as the import spec's section 3 says **[MOVED 2026-10-23 to docs/technical-debt.md as TD-3]**
126. GeoJSON import: `slideio.lineWidth` and `fillOpacity` are converted to float before the range check, which is undefined for a value like 1e300 **[MOVED 2026-10-23 to docs/technical-debt.md as TD-4]**
127. Measurements CSV (2026-10-23): the export counts a slide as calibrated when both pixel sizes are finite and > 0, the properties panel only when they are > 0, yet the header and spec call it "the properties panel's test" **[DEFERRED 2026-10-23 - only an infinite pixel size tells them apart; the export's rule is the better one, and the panel or the comment should follow it]**
128. Measurements CSV: a point's length cell is empty, but the properties panel shows "Length 0 px" for a selected point **[DEFERRED 2026-10-23 - the empty cell is the spec's deliberate "counted, not measured"; the panel is the side to change]**
129. Measurements CSV: `asciiTrimmed` is a third copy of the ASCII trim (with `AnnotationGeoJson.cpp`'s and `AnnotationListPolicy.cpp`'s `trimmedText`, beside core's `trimmed`), so the whitespace set is defined in four places **[FIXED 2026-10-23 - core::trimAsciiWhitespace and core::isBlankOrUnclassified are the one trim and the one rule; the tally, both exports and the import call them]**
130. `MainWindow.cpp` says the counts export is "paired with closeAction at all three sites"; there are four counting construction **[FIXED 2026-10-23 - the comment names the four sites and the three exports]**
131. Measurements CSV: fixed notation writes an extreme value such as 1e300 as about 300 digits **[DEFERRED 2026-10-23 - correct and round-trips; reachable only from hand-edited extreme coordinates]**
132. Workspace switch (2026-10-23 code review): when a switch fails to write and `beginSlide` in the new root then finds an unreadable file, `WorkspaceSwitch` puts the user's annotations back with `replaceAll`, but `AnnotationPersistenceService::annotationsLoaded()` stays false, so all three exports refuse with "not loaded" while every annotation is on screen **[DEFERRED 2026-10-23 - needs two failures in a row; the fix is to let the switch tell the service the model holds the scene]**
133. Both CSV exports write `formatIso8601Utc`'s result without checking it, so an export time it cannot represent would leave `exported_at` blank on every row where the GeoJSON export refuses **[DEFERRED 2026-10-23 - the export time is always now, which is always representable]**
134. ASAP import (2026-10-23): a UTF-16 XML file -- FF FE or FE FF byte-order mark -- is sent to the GeoJSON reader, whose "not valid JSON" points at the wrong format **[FIXED 2026-10-23 - detection recognises a UTF-16 byte-order mark and a < beside a zero byte]**
135. ASAP import: the summary's "already in this scene (same id)" is wrong for ASAP, whose annotations match by derived content, not an id **[FIXED 2026-10-23 - the line reads "already in this scene" for both formats]**
136. ASAP import: `parseColor` is a second #RRGGBB parser beside the serializer's #RRGGBBAA one **[FIXED 2026-10-23 - core::parseRgbColor shares the serializer's hex reading (parseHexChannels)]**
137. ASAP import: `failure`, `cancelled` and `cancelRequested` duplicate the GeoJSON reader's `importFailure`, `cancelledRead` and `cancelRequested` **[FIXED 2026-10-23 - core::importFailure, cancelledImport and importCancelRequested, used by both readers]**
138. The shared import summary is still named for GeoJSON (`GeoJsonImportSummaryInput`, `geoJsonImportSummary`, the "GeoJsonImport" translation context, `AnnotationGeoJsonImport.{h,cpp}`) though it now prints ASAP lines **[FIXED 2026-10-23 - renamed AnnotationImportSummary, ImportSummaryInput, importSummary, context "AnnotationImport"]**
139. ASAP import: `Order` is parsed without the trim and leading-'+' tolerance X and Y get, so `Order=" 0"` silently falls back to document order **[FIXED 2026-10-23 - Order is trimmed and takes a leading +, as X and Y do]**
140. ASAP import: the group colour is looked up by the untrimmed `PartOfGroup` while the classification uses the trimmed one, so `" Tumor"` gets the Tumor class but not its colour **[FIXED 2026-10-23 - groups are keyed and looked up by the trimmed name]**
141. ASAP import: each annotation's seed holds its full coordinate text, kept twice beside the raw coordinates, so peak memory is several times the file size **[FIXED 2026-10-23 - the seed is the SHA-1 of the content, 40 characters whatever its size]**
142. Text annotation: Undo or Redo with a text editor open interacted badly with the commit of its words -- committing before Redo recorded a new step and emptied the redo stack **[FIXED 2026-10-24 - Redo commits the words after redoing; Undo commits first, as before; opening a menu no longer commits at all]**
143. Text annotation: Ctrl+Z in a freshly opened, empty editor does nothing — it goes to the QLineEdit, whose history is empty — so the previous annotation cannot be undone without leaving the editor **[DEFERRED 2026-10-24]**
144. Text annotation: the editor's width is set only when the viewport repaints, so long words scroll inside a 160 px box until the next pan **[DEFERRED 2026-10-24 - connect textChanged to positionTextEditor]**
145. The properties panel shows "Length 0.00 µm" for a text, as it already did for a point, where the measurements CSV leaves the cell empty because a zero reads as a finding **[DEFERRED 2026-10-24]**
146. GeoJSON export writes a classified text as a classified QuPath Point, which QuPath counts as a finding of that class, against "a text is not a finding" **[DEFERRED 2026-10-24 - leave classification off text features, or say so in the GeoJSON spec]**
147. Text annotation: a click on an annotation-list row while typing commits the words, and the commit rebuilds the list and scrolls it to the new text before Qt delivers the press, so the press may land on a different row **[NOT REPRODUCED 2026-10-24 - the owner tried it by hand]**
148. Text annotation: under the Text tool a click on an existing text opens a new, empty text stacked on it; editing the words needs the Navigate tool and a double-click **[FIXED 2026-10-24 - reproduced by the owner; a Text-tool click on a text edits it (ui::textToolEditTarget), anywhere else -- a shape included -- still places a new one]**
149. Calibration: OK on an untouched dialog stores the 4-decimal-rounded metadata prefill as a manual override, so a scene the user did not change becomes "manual calibration" (error at most ~0.02%, and badged) **[DEFERRED 2026-10-24 - disable OK until edited, or treat an unchanged prefill as Cancel]**
150. Calibration: Slide Properties' "Width (µm)"/"Height (µm)" rows are physical lengths derived from the metadata and carry no source; beside "Calibration: manual" they disagree with every measurement, and on an uncalibrated slide with an override they read "(unknown)" **[DEFERRED 2026-10-24 - label them "(metadata)" or derive them from the calibration]**
151. Calibration: the "no calibration" status-bar prompt is overwritten at once on a read-only slide by the read-only reason, which applyAnnotationAvailability shows just after **[DEFERRED 2026-10-24 - the badge and "Uncalibrated" scale bar still say it]**
152. Calibration: the "no calibration" prompt repeats on every reopen of the same scene (ICC profile change, colour-management toggle), not only on the first open **[DEFERRED 2026-10-24]**
153. Calibration: an override made invalid by hand-editing the registry cannot be removed from the UI -- it is ignored, so Reset to metadata is disabled **[DEFERRED 2026-10-24 - harmless: the value is never used]**
154. Calibration: the measurements CSV's new `calibration_source` column sits mid-row, after `pixel_height_um`, so a reader indexing columns by position breaks **[ACCEPTED 2026-10-24 - deliberate in the spec; belongs in the release notes]**
155. Calibration: the dialog's spin boxes parse in the system locale; on a locale whose decimal separator is a comma, a typed "0.25" may not read as 0.25 -- unverified, and a misread would be a valid, badged, wrong value **[OPEN 2026-10-24 - a manual check on a German-locale machine]**
