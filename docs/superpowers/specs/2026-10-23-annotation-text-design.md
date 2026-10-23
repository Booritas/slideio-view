# Text annotation — design

Stage 3, the text shape; FR-ANN-10 (Must): "Text Label — Click to place, inline
text editor (no dialog)." Written 2026-10-23 without a question round — the
project owner asked for designs to be written directly — so every decision
below is the author's call unless it is quoted from the requirements or the
code, and is marked as such where it is a judgement rather than a consequence.

## 1. Why

Every annotation the viewer has today is geometry: a region, a line, a marker,
an arrow. A pathologist also writes *on* the slide — "invasive carcinoma",
"margin 2 mm", "re-cut?" — and today the only place for words is a shape's
label or notes, which nothing paints. The UI design names the tool and its
behaviour (`documents/02-user-interface-design.md`, "Text Label (T)"):

- Click to place; an inline text editor appears immediately (no dialog).
- Press **Enter** to confirm, **Escape** to cancel.
- Font size is defined in screen-space points but the anchor position is in
  slide coordinates.

## 2. Goals, non-goals, anti-goals

**Goals**

- A **Text** tool, shortcut **T**: click anywhere on the slide, type, Enter —
  the words are on the slide, anchored where you clicked, readable at every
  zoom.
- Text annotations behave like every other annotation: selected, moved,
  deleted, undone, autosaved, listed, edited in the properties panel, locked
  out on a read-only slide.
- Double-click a text annotation to edit its words in place.
- They survive every file the viewer writes and reads back: the annotation
  file, the GeoJSON export and import, and the measurements CSV.

**Non-goals**

- **Multi-line text.** One line, as the inline editor and the label field are.
- **Per-annotation font size, font face, bold or italic.** One size for all
  text, in screen points (§3).
- **Text inside or attached to another shape** (a label painted on a polygon).
  A separate feature if wanted; the label field of other shapes stays unpainted.
- **Rotated text, leader lines, callouts.** An arrow plus a text does that.
- **The angle tool and the area/ruler tools** named beside it in the UI design.

**Anti-goals** — it fails even if it "works" when:

- **A note inflates a clinical count.** Text is not a finding; the counting
  strip and the counts export must not count it (§3).
- **Typed words are lost** by a stray click, a tool change or a slide switch.
- **A text annotation can become invisible and unselectable** — with its words
  deleted, say, or zoomed far out.
- **An older build silently drops text annotations** on rewrite. (It must
  refuse the file and leave it intact, as it does for any shape it does not
  know — see §3, "The file format".)

## 3. What it is

### The shape

A new geometry alternative, **`TextGeometry { PointF anchor; }`**, appended to
the variant. Not a `PointGeometry` with a flag: `core::typeOf()` derives the
type from the variant alternative (CLAUDE.md), so a text and a marker must be
different alternatives or they are indistinguishable.

- **`outline()`** is the anchor alone, open; **`boundingBox()`** a zero-size box
  at it; **`translated()`** moves the anchor; **`hasWellDefinedArea()`** false;
  **`measure()`** gives no measurement, as for a point.
- **`core::hitTest()`** accepts a press within tolerance of the anchor. The
  words themselves are hit in the ui (below), because their extent is in screen
  pixels and core has no font metrics.
- **Box resize, vertex editing**: neither applies; `withBoxReplaced` answers
  `std::nullopt`, as for a point and an arrow.
- **`AnnotationType::Text`**, display name "Text".

### The words are the label

**The text a text annotation shows is its `properties.label`** (author's call).
The label is already what the list panel shows, what the properties panel
edits, what the annotation file stores, what the GeoJSON export writes as
QuPath's `name`, and what the measurements CSV writes in `label`. Making it the
displayed text means every one of those works unchanged, and there is no second
"content" field to keep in step with it. For every other shape the label stays
what it is today, unpainted.

### Drawing it

- **Font**: the application's default UI font at **12 points**, fixed in screen
  space (author's call), as FR-ANN-10 and the UI design ask. It does not grow
  with zoom: a note must stay readable whether the slide is at 1× or 40×.
- **Where**: a small **anchor dot** (the marker's disc, half size) at the
  anchor, and the words to its right, vertically centred on it, 6 screen pixels
  away.
- **Legibility over tissue**: the words in the annotation's colour, on a
  rounded **plate** of white at the annotation's **fill opacity** (default
  30%) with a 1-pixel outline in the annotation's colour at the line width.
  H&E is pink and purple; colour alone on it is often unreadable.
- **Empty words** — only reachable by clearing the label in the properties
  panel — paint the anchor dot and an italic grey "(empty)" in the plate, so
  the annotation stays visible and selectable (anti-goal). Creation never
  produces one: Enter on an empty editor cancels.
- **Selected**: the plate's outline drawn as the ordinary selection stroke, as
  a marker is ringed (CLAUDE.md, "A marker and an arrowhead are sized in screen
  pixels").
- The painter needs the same rectangle the hit test uses, so **one ui function
  computes a text annotation's screen rectangle** from its anchor, its words and
  the font, and both read it.

### Hitting it

`AnnotationModel::hitTest` stays geometric (slide units, tolerance). The
viewport adds one step: it also tests each text annotation's **screen
rectangle** (the plate) against the cursor, and **the topmost of the two
candidates wins** by model order — paint order — so a text drawn after a
polygon it sits on is the one a click finds, and a polygon drawn after a text
over it still wins there. The anchor dot is covered by core's tolerance test.

### Creating it

- **The Text tool**, shortcut **T**, with the other drawing tools — disabled on
  a read-only slide by `applyAnnotationAvailability`, the same three layers as
  the others (CLAUDE.md, "Every path that creates an annotation asks
  `annotationsEditable()` first").
- **Click** places an inline **`QLineEdit`** at the click, styled like the
  plate, with focus. Nothing is in the model yet — like a pending multi-click
  shape, it is not an annotation until committed.
- **Enter** commits: if the trimmed words are non-empty, the annotation is
  created **with its words as one undo step** ("Add text") — not a create
  followed by a property edit — and autosaved as any creation. Empty words
  commit nothing.
- **Escape** cancels: nothing is created.
- **Every other way the editor can lose focus commits it** (author's call): a
  click elsewhere, a tool change, switching windows, closing or switching the
  slide. Words someone typed are the thing most worth not losing (anti-goal);
  Escape is the deliberate way out. Closing the slide commits through the
  existing `aboutToCloseSlide` path, **before** `closeSlide()`'s flush, so the
  words are in the file that is written.
- After a commit the new text is selected, as every creation is.

### Editing it

- **Double-click a text annotation** (select tool) opens the same inline editor
  over it, pre-filled; Enter, focus loss and Escape behave as above. A changed
  text is **one undo step** ("Edit text") through the existing property-edit
  command; an unchanged one records nothing; clearing it to empty is refused —
  the old words stay (author's call: deleting the annotation is Delete).
  Double-click on a polygon or line still enters vertex editing; the two never
  meet, since one is text and the other is not.
- **The properties panel's Label field** edits the same words, as for any
  annotation.
- **Move** by dragging the plate or the dot, as any shape moves — one undo step,
  the existing move command.
- On a read-only slide double-click opens nothing, and the properties panel's
  field is disabled, as today.

### Counting: text is not a finding

**Text annotations are excluded from the counting strip** (`ui::categoryTally`)
and therefore from the counts CSV, which is built from the same call (author's
call; anti-goal). Without this every note lands in "Unclassified" or its class
and changes a clinical count. CLAUDE.md's statement that the counts "sum to the
scene's annotation count" becomes "sum to the scene's non-text annotation
count", and the strip and its export say so where they say the former.

They **are** rows in the measurements CSV — every annotation is a row there
(that spec's §2) — with `type` `text`, the anchor as the centre, the words in
`label`, and every measurement column empty, like a point.

### The file format

- Saved as `"type": "text"`, `"geometry": {"type": "text", "anchor": {"x": …,
  "y": …}}`, the label in `properties.label` as for every shape.
- **`schemaVersion` does not move** (CLAUDE.md, "`schemaVersion` does not move
  when a geometry type is added"). An older build meeting a text annotation
  refuses the file naming `text` and leaves every byte unchanged — observed for
  polygons in check P14; check AJ12 observes it again for text.
- **`readGeometry` must get its `text` branch**: it is the one dispatch site no
  compiler enforces (CLAUDE.md). The round-trip test in §5 is what forces it.

### GeoJSON

- **Export**: a `Point` at the anchor, `properties.name` = the words (QuPath
  shows it), `slideio.type` = `text`. QuPath has no text object; a named point
  is the nearest, and it is where the note was.
- **Import**: a `Point` whose `slideio.type` is `text` comes back as a text
  annotation, its words from `name`. Any other `Point` is a marker, as today.

### Dispatch sites

`TextGeometry` must be answered at every exhaustive `std::visit` over the
variant and every switch over `AnnotationType`. CLAUDE.md counted thirteen for
"the next shape"; the GeoJSON export's `GeometryVisitor` has since made it at
least fourteen. **The count is the compiler's**, not this document's: the
builder lets each missing overload fail the build, and adds `readGeometry`'s
branch by hand.

## 4. Where the code lives

- `src/core/.../AnnotationGeometry.{h,cpp}` — `TextGeometry`, `AnnotationType::Text`,
  the six functions' overloads.
- `src/core/src/AnnotationSerialization.cpp` — `text` in `geometryTypeName`,
  `shapeToJson`, `isRepresentableShape`, and `readGeometry`'s branch.
- `src/core/src/AnnotationDecoration.cpp` — the anchor dot.
- `src/core/src/BoxResize.cpp` — `std::nullopt`.
- `src/core/src/AnnotationGeoJson.cpp` — export and import mapping.
- `src/ui/src/AnnotationListPolicy.cpp` — `categoryTally` skips text.
- `src/ui/src/AnnotationShapeName.cpp` — "Text".
- `src/ui/src/AnnotationMeasurementExport.cpp` — `type` `text` (via the shared
  type name), no measurements.
- A new pure ui unit for the text's screen rectangle and the
  topmost-of-two-candidates hit rule, so both are testable without a widget.
- `src/ui/src/ViewportWidget.cpp` — the tool, the inline editor, painting,
  the hit step, double-click editing.
- `src/ui/src/MainWindow.cpp` — the action, shortcut T, availability.

## 5. Tests

Core-tests:

1. `typeOf(TextGeometry)` is `Text`; outline, bounding box, translate, area and
   measure behave as §3 says; `hitTest` accepts within tolerance of the anchor
   and refuses outside it.
2. **Serializer round trip**: a text annotation with Greek words saves and
   loads back identical — the test that forces `readGeometry`'s branch.
3. A file holding a `text` annotation is refused, intact, by a reader that does
   not know `text` — simulated by a type name the reader does not know, as the
   existing unknown-type test does.
4. `withBoxReplaced` on a text is `std::nullopt`; `screenDecoration` gives the
   anchor dot.
5. GeoJSON export writes a `Point`, `name` and `slideio.type` `text`; importing
   it returns a text annotation with the same words and anchor; a plain `Point`
   still imports as a marker.

Ui-tests:

6. `categoryTally` ignores text annotations in every row, including
   Unclassified; a classified text does not appear in its class's count.
7. The measurements CSV writes a text as `type` `text` with the anchor and
   empty measurements.
8. The screen-rectangle function: to the right of the anchor, grows with the
   words, is the same size at every zoom, and an empty text still has a
   rectangle.
9. The hit rule: the later of a geometric hit and a text hit wins, either way
   round; no text hit falls back to the geometric one.
10. The shape name is "Text".

## 6. Manual checks — group AJ

Written before the run.

| | Check | Result |
|---|---|---|
| AJ1 | T selects the Text tool; on a read-only slide the tool is disabled and T does nothing. | |
| AJ2 | Click, type "Invasive carcinoma", Enter: the words appear at the click, in the annotation colour on a translucent plate; the list shows a Text row with those words. | |
| AJ3 | Zoom from 1× to 40× and back: the words stay the same size on screen and stay at the clicked tissue. | |
| AJ4 | Click, type, Escape: nothing is created. Click, Enter with nothing typed: nothing is created. | |
| AJ5 | Click, type, then click elsewhere / change tool / Ctrl+W: the words are kept in each case, and after reopening the slide they are there. | |
| AJ6 | Undo after creating a text removes it in one step; redo restores it with its words. | |
| AJ7 | Double-click a text, change its words, Enter: one undo step reverts to the old words; clearing the words and pressing Enter keeps the old words. | |
| AJ8 | Drag a text by its plate: it moves, one undo step. A text over a polygon drawn earlier is the one a click selects; a polygon drawn later over a text wins where they overlap. | |
| AJ9 | Clear a text's label in the properties panel: the annotation still shows its dot and "(empty)" and can be selected and deleted. | |
| AJ10 | With a text classified Tumor and markers counted under Tumor, the counting strip's Tumor count excludes the text, and so does Export Counts. | |
| AJ11 | Export GeoJSON and Measurements: the text is a named Point with `slideio.type` `text`, and a measurements row of type `text`; importing the GeoJSON into an empty scene brings the text back as text. | |
| AJ12 | An annotation file with a text annotation, opened by a build from before this change: refused naming `text`, and the file unchanged byte for byte. | |

## 7. Requirements

| Requirement | How |
|---|---|
| FR-ANN-10 — Text Label: click to place, inline editor, no dialog | This design. |
| FR-ANN-11 — label text on every annotation | For text, the label is the displayed words. |
| FR-ANN-16/17/18 — select, move, undo | Unchanged machinery; text has no resize or vertex editing. |

## 8. Left to the builder

- Names of the new ui unit and its functions.
- The plate's exact padding and corner radius, and the anchor dot's exact size,
  within §3.
- How the inline editor is parented and positioned, as long as it follows the
  anchor if the view is panned while it is open, or commits on pan.
- The exact undo-step descriptions.
