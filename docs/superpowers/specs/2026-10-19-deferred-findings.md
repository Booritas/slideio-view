# Deferred minor findings — recovered index

Status: recovered 2026-10-19 from the session transcript; split into seven
batches, B1 to B3 done; 46 of 117 triaged, 12 fixed. **The full text of these
findings is lost.** What is here is each one's opening clause, enough to identify
it and re-derive it against the code, and nothing more.

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
| B4 | panels, categories, preferences | 13, 15, 18, 28, 32, 61, 64, 67, 69, 81, 83, 85, 86, 109 | 14 | not started |
| B5 | persistence, paths, workspace, serialization | 12, 22, 23, 31, 36, 38, 39, 40, 59, 94, 96, 99, 100, 114, 117 | 15 | not started |
| B6 | documentation and comments | 4, 17, 20, 35, 41, 48, 63, 74, 75, 78, 79, 80, 90, 92, 95, 108, 113 | 17 | not started |
| B7 | test quality: weak, missing or vacuous cases | 26, 45, 47, 49, 50, 51, 52, 53, 54, 55, 56, 77, 82, 87, 89, 91, 93, 97, 98, 101, 102, 103, 105, 115 | 24 | not started |

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
covers ASCII, the Latin-1 Supplement and Latin Extended-A, applied to UTF-8
bytes; everything else passes through. That is narrower than
`QString::toCaseFolded` -- Greek, Cyrillic and the Turkish dotless i fold to
themselves -- and the narrowing was chosen deliberately: agreement across the
boundary is worth more than width, and the width is only lost where the ASCII
version had already lost it. `std::tolower` and `std::isspace` are gone from
both files for the same reason they were wrong: they read the global C locale,
and under a single-byte one they rewrite or trim the lead byte of a UTF-8
letter depending on whether something else in the process had called
`setlocale`.

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

## The findings

1. #10 moveOriginalGeometry's unguarded `if (picked)` could **[CLOSED - the else branch resets moveOriginal, ViewportWidget.cpp:4675, so a capture from an earlier drag cannot be restored onto this annotation]**
2. #11 the front-trim loop erases one element at a time, O(n) **[CLOSED - UndoRedoStack.cpp:39-46 is one range erase, with the reasoning in a comment]**
3. #4 setSelected() unconditional after an insert() that can drop **[CLOSED - AnnotationModel::setSelected refuses an id the model does not hold, and AnnotationEditCommand::apply says so at the call: a dropped insert leaves the previous selection standing]**
4. #4 Shift+Ctrl squares the box (code correct, plan
5. #5 an invalid command is rejected by convention at the call **[CLOSED - AnnotationEditCommand::create rejects a command recording neither state, centrally, so a push site added later cannot forget]**
6. #6 ~MainWindow nulls the persistence pointer but not **[CLOSED - the three Qt-owned widgets are deleted and nulled explicitly, after saveState; every app and infra service is a unique_ptr in Impl and dies with it]**
7. #6 preview omits withMinimumExtent that the commit **[ACCEPTED 2026-10-20 - and now stated where the preview is painted: the preview shows the gesture, and the one-screen-pixel widen is what the commit does to a gesture collapsed on one axis]**
8. #7 normalized(EllipseGeometry) duplicates the **[FIXED 2026-10-19 - normalizedCorners(PointF, PointF) is shared by rectangle, ellipse and the arrow box]**
9. #8 the pi literal is repeated three times in **[CLOSED - single constexpr kPi at AnnotationGeometry.cpp:15]**
10. #8 undoing a create clears an unrelated selection -- exactly **[CLOSED - AnnotationModel::remove clears the selection only when the removed id IS the selected one, AnnotationModel.cpp:140]**
11. a 1-pixel jitter counts as a pan and suppresses **[CLOSED - the selection test is the press-to-release distance, not whether a move event arrived; now in ui::releaseSelectsAnnotation]**
12. a hand-edited file carrying fillOpacity 0.333 quantises and
13. accepting Preferences writes the category store even when
14. an empty polygon's boundingBox is {0,0,0,0} — a real rect at **[ACCEPTED 2026-10-19 - documented as a sentinel at AnnotationGeometry.cpp:76; no reachable path builds an empty polygon, so std::optional would make every caller handle a case none can meet]**
15. an empty/whitespace-only `to` retags to an empty
16. AnnotationEditing.cpp uses std::nullopt with only an **[FIXED 2026-10-20 - <optional> is included directly; it had been reaching it through AnnotationEditCommand.h]**
17. AnnotationGeometry.h header comment 
18. AnnotationPropertiesPanel.cpp:304 dereferences widget()->layout()
19. annotationsBulkChanged -> refreshAfterReset forces stored **[CLOSED - AnnotationPropertiesPanel.cpp:232 connects the bulk signal to refresh, not refreshAfterReset, so a field the user has focus in stays theirs]**
20. AnnotationShapeName.cpp breaks alphabetical order in
21. AnnotationType and the geometry variant are independent **[ACCEPTED 2026-10-19 - the dangerous direction is a compile error: a new variant alternative with no typeOfShape overload will not build. The reverse leaves an unreachable enumerator]**
22. bad_alloc from the outer catch is reported as
23. beginSlide's file read is on the UI thread; a
24. categories() does not normalise on read, so a hand-edited **[CLOSED - categories() calls core::normalizeCategoryList on read, QSettingsAnnotationCategoryStore.cpp:44]**
25. categoryTally recomputes on every filter keystroke though **[FIXED 2026-10-19 - rebuild() split; the filter and sort combos call rebuildRows(), which does not touch the tally]**
26. ceiling test 2 is vacuous on MSVC and libc++ — its
27. CLAUDE.md claims the ui layer links slideio-viewer-app; it **[appears CLOSED 2026-10-19 - CMake and CLAUDE.md agree today]**
28. clearing the user name (setUserName(
29. collinearity is exact (d == 0.0), no epsilon. Correct and **[ACCEPTED 2026-10-19 - an epsilon would make "collinear" depend on coordinate magnitude, so a shape would change its self-intersection answer by being panned]**
30. core::nearestBoxHandle treats a NaN distance as in **[CLOSED - BoxResize.cpp:35-39 refuses NaN deliberately]**
31. currentSlideId() recomputes the content-size walk that
32. displacedProfileNote takes a const SlideInfo&, so
33. Escape is accepted unconditionally, blocking any parent **[CLOSED - with nothing in flight it falls through to QOpenGLWidget::keyPressEvent, ViewportWidget.cpp:5266]**
34. filter folds case but the Label/Classification/Type sorts **[FIXED 2026-10-19 - the three sorts fold case, tests in AnnotationListPolicyTest.cpp]**
35. formatIso8601Utc's header comment names only the 9999
36. if DocumentsLocation resolves empty (rare Linux), the
37. item->setData(0, Qt::UserRole, row.configured) is a dead **[CLOSED - no such call remains]**
38. M3 — `previousWorkspace` captures the resolved path, so
39. M5 — `result.asked` is set before the resolver check,
40. M6 — Cancel re-enters beginSlide, which re-runs the
41. MainWindow.cpp now 1191 lines; mirrors its existing pattern
42. measure() calls outline(geometry) and then measureKnown calls **[FIXED 2026-10-19 - measureOutline takes the outline measure() already built; the double build fell on freehand traces, the shapes with the most vertices]**
43. move rebuilds geometry as RectangleGeometry from the **[CLOSED - the move applies core::translated to the captured geometry, ViewportWidget.cpp:4925]**
44. NaN behaviour is benign but accidental. std::min(NaN, 5.0f) **[ACCEPTED 2026-10-19 - benign and now stated: AnnotationDecoration.cpp returns the fixed head size, so the glyph draws nominally rather than vanishing]**
45. NaN geometry inputs untested
46. NaN in lineWidth/fillOpacity always compares unequal, so it **[CLOSED - inClosedRange calls std::isfinite first, AnnotationSerialization.cpp:103, so NaN is refused on both read and write]**
47. new #includes/usings sit mid-file in the test, against the
48. new isUnclassifiedCategory declaration was inserted between
49. new test names start lowercase; the file's existing cases start
50. no exact-boundary test — ceiling accepted vs ceiling+1
51. no golden-vector test pins the exact FNV-1a-over-separated-string
52. no test for a duplicate matching only after trim AND case
53. no test has two annotations with differing modifiedAt.
54. no test pins that a stored classification with surrounding
55. no test that an unset store leaves the file unwritten, nor
56. no tests for case-duplicate or whitespace-padded CONFIGURED
57. no upper bound on scale — a sub-pixel bounding box yields a **[CLOSED - ViewportController.cpp:99 clamps to kMinScale..kMaxScale right after fitToRect, with a comment saying core knows nothing of the limits]**
58. nothing resets dragOwner when a slide closes mid-drag; **[CLOSED - resetAnnotationState clears dragOwner, dragButton and every captured original, then calls updateCursor]**
59. openAuxImage has no live caller today (associated images go via
60. paintAnnotations has no per-type geometry dispatch; needed **[CLOSED - it paints from core::outline and core::screenDecoration, so the dispatch is in core where the variant is]**
61. parallel `rows`/`keys` vectors could desynchronise if a
62. preview rectangle repeats the screenRect conversion. **[FIXED 2026-10-20 - the preview draws screenPath(core::outline(...)); the hand-rolled ellipse path loop is gone]**
63. ragged comment line-wrapping left by the edits
64. rebuild() runs on every annotationChanged. If a move-drag
65. rectangles are not clamped to the slide extent. Recorded as **[LIVE, ACCEPTED 2026-10-19 - no clamp exists. A trace running off the tissue edge is a real gesture and the slide extent is not a clinical boundary; clamping would move a vertex the user placed]**
66. rename is refused entirely on a read-only slide including the
67. rename with no slide open reports 
68. resizableBoxGeometry computes boundingBox before the **[STILL LIVE - ViewportWidget.cpp:1641 vs :1645]**
69. setAnnotationModel(nullptr) on an already-null model returns
70. snprintf buffer truncation rather than overrun for extreme **[FIXED 2026-10-19 - kNumberBufferSize is 352, enough for %.0f of DBL_MAX; see 104]**
71. Space held + left click with no movement clears the **[FIXED 2026-10-20 - ui::releaseSelectsAnnotation refuses a click made under held Space; a pan the user did not ask for, such as a read-only slide's, still selects]**
72. std::tolower/std::isspace are locale-dependent where the **[FIXED 2026-10-19 - core::caseFoldKey replaces both, and ui now calls it too; this was the core half of 106]**
73. tally not cleared when the model goes null (unreachable today). **[CLOSED - rebuildTally() clears before the null check]**
74. task-2-report.md per-case assertion breakdown lists cases
75. task-3-report.md file-size table reports 66/65/108 lines
76. that condition is now DUPLICATED between the preview **[FIXED 2026-10-20 - ui::boxToolGeometry is the one place deciding whether a box drag is an ellipse or a rectangle; preview and commit both ask it]**
77. the 0.45 margin clamp has no test of its own.
78. the 720->900px widen + resizeColumnsToContents judged JUSTIFIED,
79. the ambiguity fix was correct and test-only; no production change
80. the comment at ViewportWidget.cpp:4201 (
81. the context-menu handler is correct only because nothing reads
82. the cross-session persistence test would pass with
83. the dedup `seen` vector is searched linearly (O(n^2)); fine
84. the dragOwner reset in commitPendingShape is unconditional, so **[CLOSED - endToolDrag returns early unless dragOwner == Tool, so a pan stays alive across a commit, a cancel or a tool change]**
85. the empty state clears the measurement VALUES but does not
86. the file-pick+validate block in onSetSlideColorProfile duplicates
87. the first Annotation test is also weak against a
88. the hint is transient and set on activeToolChanged, so a **[LIVE, ACCEPTED 2026-10-20 - setActiveTool has no early return, so re-pressing the tool key re-shows it. Making it persist for the length of a trace is a design decision, not a defect fix]**
89. the inverted-rectangle outline test asserts corners 0 and 2
90. the movePressSlide comment's 
91. the negative test covers rectangle and arrow only; an
92. the new include is one position out of alphabetical order in a
93. the normalisation test covers one case; core's tests cover
94. the pacing fix (m_lastMutation push-forward) is
95. the Preferences tooltip says 
96. the quit cannot be aborted — see R27.
97. the refused-save test does not pin the serialize-first
98. the second new test (getter vs static resolver) is
99. the serializer will write an empty annotation id that
100. the suppliedWasUsed block is duplicated verbatim across both
101. the tie-break tests depend on std::sort actually reordering
102. the unchanged-edit test's modifiedAt equality could pass
103. the zero-translation identity test asserts x and y but not
104. threeSignificantFigures can ask for ~302 decimals at a **[FIXED 2026-10-19 - decimals clamped to kMaxDecimals, and a non-finite guard added: log10(inf) cast to int was undefined behaviour]**
105. tolerance on a degenerate rectangle untested
106. toLower is ASCII-only, so non-ASCII case variants split rows **[FIXED 2026-10-19 - toLower uses QString::toCaseFolded; test uses an O-diaeresis pair]**
107. toLower(clean) computed twice per non-empty classification. **[FIXED 2026-10-19 - the key is computed after the refusals that discard it]**
108. trailing whitespace on the loadFailure line in load().
109. two hand-written QStringList<->vector<string> conversion
110. ui uses QString case-insensitive comparison where core uses **[PARTLY FIXED 2026-10-19 - ui now folds case the Unicode way; core still folds ASCII, which is sufficient for its ASCII-only comparand]**
111. unused <QSettings> include in the test file. **[CLOSED - QSettings is used at QSettingsAnnotationCategoryStoreTest.cpp:84]**
112. updateCursor has no DragOwner::MoveVertex case, so a **[CLOSED - the case exists at ViewportWidget.cpp:3862 with the reasoning beside it]**
113. Viewport.h's doc comment does not mention the [0, 0.45]
114. while the service is null the two emissions are
115. whitespace asymmetry in the (none) test — 
116. window title still names the closed slide after File > Close **[CLOSED 2026-10-19 - commit 3baecc2]**
117. Windows reserved device names (CON, NUL, AUX, PRN,

