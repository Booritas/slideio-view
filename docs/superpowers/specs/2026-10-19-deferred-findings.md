# Deferred minor findings — recovered index

Status: recovered 2026-10-19 from the session transcript. **The full text of
these findings is lost.** What is here is each one's opening clause, enough to
identify it and re-derive it against the code, and nothing more.

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

**Nothing here has been triaged.** Four were spot-checked on 2026-10-19 to test
whether the index is usable, and the sample suggests a meaningful fraction is
already closed:

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

## The findings

1. #10 moveOriginalGeometry's unguarded `if (picked)` could
2. #11 the front-trim loop erases one element at a time, O(n)
3. #4 setSelected() unconditional after an insert() that can drop
4. #4 Shift+Ctrl squares the box (code correct, plan
5. #5 an invalid command is rejected by convention at the call
6. #6 ~MainWindow nulls the persistence pointer but not
7. #6 preview omits withMinimumExtent that the commit
8. #7 normalized(EllipseGeometry) duplicates the
9. #8 the pi literal is repeated three times in
10. #8 undoing a create clears an unrelated selection -- exactly
11. a 1-pixel jitter counts as a pan and suppresses
12. a hand-edited file carrying fillOpacity 0.333 quantises and
13. accepting Preferences writes the category store even when
14. an empty polygon's boundingBox is {0,0,0,0} — a real rect at
15. an empty/whitespace-only `to` retags to an empty
16. AnnotationEditing.cpp uses std::nullopt with only an
17. AnnotationGeometry.h header comment 
18. AnnotationPropertiesPanel.cpp:304 dereferences widget()->layout()
19. annotationsBulkChanged -> refreshAfterReset forces stored
20. AnnotationShapeName.cpp breaks alphabetical order in
21. AnnotationType and the geometry variant are independent
22. bad_alloc from the outer catch is reported as
23. beginSlide's file read is on the UI thread; a
24. categories() does not normalise on read, so a hand-edited
25. categoryTally recomputes on every filter keystroke though
26. ceiling test 2 is vacuous on MSVC and libc++ — its
27. CLAUDE.md claims the ui layer links slideio-viewer-app; it **[appears CLOSED 2026-10-19 - CMake and CLAUDE.md agree today]**
28. clearing the user name (setUserName(
29. collinearity is exact (d == 0.0), no epsilon. Correct and
30. core::nearestBoxHandle treats a NaN distance as in **[CLOSED - BoxResize.cpp:35-39 refuses NaN deliberately]**
31. currentSlideId() recomputes the content-size walk that
32. displacedProfileNote takes a const SlideInfo&, so
33. Escape is accepted unconditionally, blocking any parent
34. filter folds case but the Label/Classification/Type sorts
35. formatIso8601Utc's header comment names only the 9999
36. if DocumentsLocation resolves empty (rare Linux), the
37. item->setData(0, Qt::UserRole, row.configured) is a dead
38. M3 — `previousWorkspace` captures the resolved path, so
39. M5 — `result.asked` is set before the resolver check,
40. M6 — Cancel re-enters beginSlide, which re-runs the
41. MainWindow.cpp now 1191 lines; mirrors its existing pattern
42. measure() calls outline(geometry) and then measureKnown calls
43. move rebuilds geometry as RectangleGeometry from the
44. NaN behaviour is benign but accidental. std::min(NaN, 5.0f)
45. NaN geometry inputs untested
46. NaN in lineWidth/fillOpacity always compares unequal, so it
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
57. no upper bound on scale — a sub-pixel bounding box yields a
58. nothing resets dragOwner when a slide closes mid-drag;
59. openAuxImage has no live caller today (associated images go via
60. paintAnnotations has no per-type geometry dispatch; needed
61. parallel `rows`/`keys` vectors could desynchronise if a
62. preview rectangle repeats the screenRect conversion.
63. ragged comment line-wrapping left by the edits
64. rebuild() runs on every annotationChanged. If a move-drag
65. rectangles are not clamped to the slide extent. Recorded as
66. rename is refused entirely on a read-only slide including the
67. rename with no slide open reports 
68. resizableBoxGeometry computes boundingBox before the **[STILL LIVE - ViewportWidget.cpp:1641 vs :1645]**
69. setAnnotationModel(nullptr) on an already-null model returns
70. snprintf buffer truncation rather than overrun for extreme
71. Space held + left click with no movement clears the
72. std::tolower/std::isspace are locale-dependent where the
73. tally not cleared when the model goes null (unreachable today).
74. task-2-report.md per-case assertion breakdown lists cases
75. task-3-report.md file-size table reports 66/65/108 lines
76. that condition is now DUPLICATED between the preview
77. the 0.45 margin clamp has no test of its own.
78. the 720->900px widen + resizeColumnsToContents judged JUSTIFIED,
79. the ambiguity fix was correct and test-only; no production change
80. the comment at ViewportWidget.cpp:4201 (
81. the context-menu handler is correct only because nothing reads
82. the cross-session persistence test would pass with
83. the dedup `seen` vector is searched linearly (O(n^2)); fine
84. the dragOwner reset in commitPendingShape is unconditional, so
85. the empty state clears the measurement VALUES but does not
86. the file-pick+validate block in onSetSlideColorProfile duplicates
87. the first Annotation test is also weak against a
88. the hint is transient and set on activeToolChanged, so a
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
104. threeSignificantFigures can ask for ~302 decimals at a
105. tolerance on a degenerate rectangle untested
106. toLower is ASCII-only, so non-ASCII case variants split rows
107. toLower(clean) computed twice per non-empty classification.
108. trailing whitespace on the loadFailure line in load().
109. two hand-written QStringList<->vector<string> conversion
110. ui uses QString case-insensitive comparison where core uses
111. unused <QSettings> include in the test file.
112. updateCursor has no DragOwner::MoveVertex case, so a
113. Viewport.h's doc comment does not mention the [0, 0.45]
114. while the service is null the two emissions are
115. whitespace asymmetry in the (none) test — 
116. window title still names the closed slide after File > Close **[CLOSED 2026-10-19 - commit 3baecc2]**
117. Windows reserved device names (CON, NUL, AUX, PRN,

