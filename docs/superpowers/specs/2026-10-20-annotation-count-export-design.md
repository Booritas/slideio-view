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
| AD1 | With no slide open, File > Export Counts is disabled. | |
| AD2 | Open a slide: it is enabled. Close the slide: disabled again. | |
| AD3 | Export a scene with counts: the file opens in Excel by double-click, with seven columns and one row per tally row. | |
| AD4 | The rows and their order are exactly what the strip was showing, Unclassified last. | |
| AD5 | The counts sum to the annotation count the dock title reports. | |
| AD6 | A category typed in Greek or Cyrillic shows correctly in Excel, not as mojibake. | |
| AD7 | A category holding a comma lands in one cell, not two. | |
| AD8 | A category named `=1+1` shows as text in the cell, not as `2`. | |
| AD9 | Type into the list's filter box, then export: the file still holds every annotation, not the filtered subset. | |
| AD10 | Export an empty scene: a header row and the configured categories at zero. | |
| AD11 | Export from a read-only slide: it works, and the counts are right. | |
| AD12 | Cancel the save dialog: no file is written and nothing is reported. | |
| AD13 | Export twice to the same name: the dialog asks before overwriting, and the second file is a complete file, not an appended one. | |
| AD14 | Export to a path that cannot be written (a read-only folder): a warning names the path and the reason, and no partial file is left. | |
| AD15 | The suggested name is `<slide>.s<N>.counts.csv` and the dialog opens in the annotation workspace. | |
| AD16 | Export scene 1 of a multi-scene slide: `scene` reads 1 and the counts are that scene's. | |
| AD17 | The status bar confirms the export and names the file. | |

## 7. Requirements

Serves the counting workflow of FR-ANN-21/22's neighbourhood without
implementing either. No requirement in
`01-software-requirements-specification.md` names an export of counts; this is
the round's own addition, recorded here rather than back-fitted to a
requirement it does not have.
