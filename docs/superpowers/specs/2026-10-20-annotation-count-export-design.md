# Export of category counts — design

Status: **implemented 2026-10-20**, group AD not yet run. The counting round
(`2026-10-18-annotation-categorized-counting-design.md`) named this in its §8 as
"the obvious next round, and the one a pathologist will ask for after using
this".

The project owner opted out of the design questions for this round and said to
start, so the decisions below are mine unless they are marked otherwise. They
are written down because an undocumented decision is one nobody can disagree
with later.

## 1. Why

A counting pass is several hundred clicks and produces a tally that exists only
on screen. Getting those numbers anywhere else today means reading them off the
strip and retyping them — by hand, into a report, with nothing recording which
slide and which scene they came from. Transcription is the step that introduces
an error nobody can find afterwards, because the only witness was the screen.

## 2. Goals, non-goals, anti-goals

**Goals.** One file, per scene, holding exactly the tally the strip is showing,
with enough provenance that a row found later on its own still says what it
counted. Openable by double-click in a spreadsheet, and parseable by a script.

**Non-goals.** Per-annotation export (every annotation with its geometry,
label and classification) — a different file with a different reader, and §8 of
the counting spec lists it separately. Import. A formatted report, PDF or
otherwise. Aggregation across slides: the file is per scene, and two files
concatenate.

**Anti-goals** — ways this fails even if it works:

- **A file that disagrees with the screen.** If the export recounted the
  annotations by its own route, the two could drift and the file would be the
  one that got filed. It calls `ui::categoryTally` on the same two inputs the
  strip uses, so there is one count, not two that agree.
- **A file that reorders the tally.** The strip is deliberately never ordered
  by count (a row that moves under a clicking hand is a misclick). A file sorted
  by count would not be the read-out the user checked.
- **A partial file reported as a success.** A counts file that is short by a row
  reads as a complete count. Hence the explicit `flush()`/`close()` and the
  byte-count check, rather than letting `~QFile` swallow the failure.
- **Mojibake.** The fold now treats Greek and Cyrillic as first-class, so a
  category can easily be non-ASCII. A file Excel opens as garbage is a file the
  user will retype.

## 3. What it is

`File > Export Counts...` opens a save dialog, defaulting into the annotation
workspace with the name `<slide file name>.s<N>.counts.csv` — the annotation
file's own `.s<N>` convention, so a counts file sorts beside the annotations it
counted.

The file is UTF-8 with a byte-order mark, CRLF-terminated, with a header row
and one row per tally row:

```
category,count,in_list,slide,scene,slide_id,exported_at
Tumor,10,true,D:/slides/Philips-1.tiff,0,a1b2c3d4e5f60718,2026-10-20T09:15:00Z
CD31,7,false,D:/slides/Philips-1.tiff,0,a1b2c3d4e5f60718,2026-10-20T09:15:00Z
Unclassified,15,false,D:/slides/Philips-1.tiff,0,a1b2c3d4e5f60718,2026-10-20T09:15:00Z
```

### The decisions, and why

- **CSV, not a formatted report.** It is what a spreadsheet opens and what a
  script reads, and both of those are real readers here.
- **A UTF-8 BOM.** Without it Excel reads a `.csv` in the machine's legacy
  codepage; with the fold now covering Greek and Cyrillic, a non-ASCII category
  is an ordinary thing to have. The BOM is part of what `categoryCountCsv`
  returns rather than something the writer prepends, so a test holds the
  decision.
- **CRLF**, which is what RFC 4180 says and what this file is for.
- **Provenance repeated on every row**, not written once in a header block. Two
  exports then paste one under the other and every row still says which slide
  and scene it counted — and a header block would have to be a comment, which
  is the one thing CSV has no agreed syntax for. `slide_id` and `scene` are the
  pair the annotation file itself is keyed by.
- **An empty `slide_id` is written as an empty field**, for a slide that could
  not be identified. That is `currentSlideId()`'s existing empty-string
  contract; the column staying is how the file says the id was unavailable.
- **No total row.** Every annotation lands in exactly one row, so the column
  sums to the scene's annotation count; a spreadsheet sums it. A total line is
  the classic thing that silently becomes a data row when a file is read back.
- **The synthetic Unclassified row is written as the word, with `in_list`
  false,** and needs no column of its own: `core::isUnclassifiedCategory`
  refuses it as a configured category, a rename source and a rename target, so
  nothing else can ever appear under that name.
- **Fields a spreadsheet would evaluate get a leading apostrophe.** A category
  is whatever the user typed into the counting combo, and Excel, LibreOffice and
  Sheets all evaluate a cell starting with `=`, `+`, `-` or `@`. This file is
  meant to be mailed. A category genuinely named `-Tumor` therefore reads back
  with an apostrophe; the trade is deliberate, because a formula is the worse of
  the two surprises. Quoting alone does **not** prevent evaluation, which is why
  the apostrophe and not just RFC 4180 escaping.
- **Available on a read-only slide.** Its counts are exactly what a reader
  opening one wants, which is the same reasoning that keeps the tally visible
  there while the counting combo is disabled. So the action is **not** in
  `applyAnnotationAvailability`; it is paired with `closeAction` and gated on a
  slide being open.
- **An empty scene exports a header and the configured rows at zero.** A count
  of zero is a finding.

## 4. Where the code lives

`ui::categoryCountCsv` and `ui::countExportFileStem` in
`src/ui/{include/slideio/viewer/ui,src}/AnnotationCountExport.{h,cpp}` — pure,
Qt-free, and therefore reachable by `ui-tests`, which is the only reason they
are not inside the `MainWindow` lambda. **19 tests.**

Not in `core`: the rows are `ui::CategoryTallyRow`, which `ui::categoryTally`
produces, and core cannot see ui. Moving the tally down to core would be the
alternative; it was rejected for this round on blast radius, not on principle,
and it is where this would go if a second consumer ever appears.

`MainWindow::Impl::exportCounts()` does the dialog, the write and the
reporting, and nothing else: no formatting decision is taken there.

## 5. Deliberate absences

- **Per-annotation export.** Counting, not inventory.
- **A chosen delimiter, or TSV.** One format, until somebody needs the other.
- **Remembering the last export folder.** The workspace is the right default
  every time; a remembered folder is right until it is wrong.
- **Exporting the filtered list.** The tally has never seen the filter text and
  must not start here — a clinical count that changed when someone typed in a
  search box would be a trap.
- **A column for the slide's scene name.** `scene` is the index the annotation
  file is keyed by; a name is a second, disagreeing way to say the same thing.

## 6. Manual checks — group AD

Nothing below is reachable from any suite: the formatter is tested, the action,
the dialog and the write are not. Written before the run, as group AC's list
was, and the reasons for any struck clause get recorded rather than left looking
like a failure.

| | Check | Result |
|---|---|---|
| AD1 | With no slide open, File > Export Counts is disabled. | Pass |
| AD2 | Open a slide: it is enabled. Close the slide: disabled again. | Pass |
| AD3 | Export a scene with counts: the file opens in Excel by double-click, with seven columns and one row per tally row. | Pass in **LibreOffice Calc** — see below |
| AD4 | The rows and their order are exactly what the strip was showing, Unclassified last. | Pass |
| AD5 | The counts sum to the annotation count the dock title reports. | Pass — 27 = "Annotations (27)" |
| AD6 | A category typed in Greek or Cyrillic shows correctly in Excel, not as mojibake. | Pass in **LibreOffice Calc** — see below |
| AD7 | A category holding a comma lands in one cell, not two. | Pass |
| AD8 | A category named `=1+1` shows as text in the cell, not as `2`. | Pass in **LibreOffice Calc** — see below |
| AD9 | Type into the list's filter box, then export: the file still holds every annotation, not the filtered subset. | Pass — "3 of 27" filtered, file still sums to 27 |
| AD10 | Export an empty scene: a header row and the configured categories at zero. | Pass — and no Unclassified row |
| AD11 | Export from a read-only slide: it works, and the counts are right. | Pass |
| AD12 | Cancel the save dialog: no file is written and nothing is reported. | Pass |
| AD13 | Export twice to the same name: the dialog asks before overwriting, and the second file is a complete file, not an appended one. | Pass |
| AD14 | Export to a path that cannot be written (a read-only folder): a warning names the path and the reason, and no partial file is left. | Pass, **through a write-denied file** — see below |
| AD15 | The suggested name is `<slide>.s<N>.counts.csv` and the dialog opens in the annotation workspace. | Pass |
| AD16 | Export scene 1 of a multi-scene slide: `scene` reads 1 and the counts are that scene's. | Pass |
| AD17 | The status bar confirms the export and names the file. | Pass |

**Run 2026-10-21 against the installed build (sha256 `f2aa48f1…`, commit
`016dfd5`): 17/17 passed, three of them in a stand-in and one by a different
route.** Driven by UI Automation and synthetic input; every file was checked
byte for byte, not only by eye.

Fixtures. Philips-1 scene 0 with 27 annotations whose classifications cover the
configured three (Tumor, Stroma, Ki67 — Stroma with none, so a zero row), four
unconfigured spellings (`Tumor, invasive`, `=1+1`, `Νέκρωση`, `Опухоль`) and nine
unclassified. The expected file was worked out from the annotation file before
the export and matched it: Tumor 8, Stroma 0, Ki67 3, `"Tumor, invasive"` 2,
`'=1+1` 1, Νέκρωση 2, Опухоль 2, Unclassified 9 — the strip's rows in the
strip's order. UTF-8 byte-order mark, CRLF on all nine lines, seven columns.
AD10 and AD16 used `scn/ultivue/Leica Aperio Versa 5 channel fluorescent
image.scn` (three scenes): scene 0 untouched, scene 1 given three Stroma and two
Ki67 markers through the counting combo and the marker tool.

**AD3, AD6, AD8 ran in LibreOffice Calc 26.8, not Excel.** Excel is not
installed on the machine; Calc is its `.csv` handler, so it is what a
double-click opens there. Calc's import dialog detected the encoding as UTF-8
on its own (from the byte-order mark), split seven columns, kept
`Tumor, invasive` in one cell, and showed Greek and Cyrillic correctly. `=1+1`
displays as `'=1+1` — the deliberate side of the trade in §3. Note that this
does NOT prove the apostrophe is what stops evaluation in Calc: Calc's import
leaves "Evaluate formulas" off by default, so it would not have evaluated a bare
`=1+1` either. The apostrophe is there for Excel and Sheets, and those three
checks stayed open for Excel itself until **2026-10-23, when the project owner
ran AD3, AD6 and AD8 in Excel: all three pass.**

**AD14 reached the application's warning through a write-denied FILE, not a
read-only folder.** With an unwritable folder the Windows save dialog refuses
first ("You don't have permission to save in this location … save in the
Documents folder instead?"), so the path never reaches `exportCounts` — that
branch is unreachable through a folder on Windows, and the folder was left
empty, which is the clause that matters. To reach the application's own write
failure, an existing file had write denied by ACL; the dialog asked to replace
it, and the export then warned "The counts could not be written to:
D:\tmp\ad-run\ad14-locked.csv — Access is denied." The file was untouched at its
original three bytes.

AD11's read-only slide came from the lock rather than a slide-ID mismatch: a
second viewer opened Philips-1 while the first held
`ec83fc2ebbfbc515.s0.annotations.json.lock`, so it showed the 27 annotations
with "Annotations disabled". Export Counts stayed enabled there and wrote the
same eight rows. This is incidentally the first exercise of FR-ANN-32's
contention path against the built viewer (group AE, not yet run, is its proper
check). Both locks were released when the viewers closed.

## 7. Requirements

Serves the counting workflow of FR-ANN-21/22's neighbourhood without
implementing either. No requirement in
`01-software-requirements-specification.md` names an export of counts; this is
the round's own addition, recorded here rather than back-fitted to a
requirement it does not have.
