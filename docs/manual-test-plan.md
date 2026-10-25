# Manual test plan

The whole application, as built on 2026-10-24. No test suite creates a `QApplication`, so nothing in
`ViewportWidget`, `MainWindow` or any dialog is reachable from ctest. This list is the only check those
layers get.

Tests that ran before keep their original ID (`P5`, `AC26`, ...) so their recorded results can still be
found. The source spec under `docs/superpowers/specs/` is named in each section heading. Areas that never
had a check group get new IDs with a short prefix (`OPEN-`, `NAV-`, ...). A section's tests share one
source unless an item names another.

Copy the file for a run and tick the boxes. Record failures under the test with what was actually seen
and the build (installed `.exe` timestamp or commit).

## Before a run

- **Test the installed binary.** Kill every running viewer, then
  `cmake --build build/build --config Release` and `cmake --install build/build --config Release`, and run
  `build/install/release/bin/slideio-viewer.exe`. A running viewer turns the install into a silent no-op.
  Before you start, compare that `.exe`'s timestamp with `build/build/Release/slideio-viewer.exe`.
- **Set a name.** Preferences > Your name must be set before any drawing test, except PS-7 and PREF-2.
- **Back up any annotation file you hand-edit.** Files are
  `<annotations workspace>/annotations/<slideId>.s<N>.annotations.json`. Use `certutil -hashfile <f> SHA256`
  when a test says "byte-identical".
- **Read the log.** File > Open Log File.

### Fixtures

| Need | Used by | Example |
|---|---|---|
| Calibrated brightfield SVS | most tests | `CMU-1-Small-Region.svs` (0.499 µm/px) |
| Uncalibrated slide | W5, AH6, AK6, AL3, SB-4 | `CMU-1-Small-Region-page-2.tif`, `...-page-0.bmp` |
| Slide with an embedded ICC profile | CM-1, ICC-2 | `svs/JP2K-33003-1.svs` |
| Slide with label/macro images | SCN-3, Item0-4, AE14 | any Aperio SVS |
| Multi-scene file | AD16, AE8, AF8, AG8, AK4, X10, SCN-1 | 3-scene Leica SCN, CZI, VSI |
| Annotated slide with 27 annotations | AF3, AG2, AH3 | `Philips-1.tiff` scene 0 |
| Fluorescence slide, Z-stack | CH-*, CM-6, NAV-8 | fluorescence VSI or CZI |
| DICOM study folder | OPEN-2 | — |
| QuPath 0.7.0 | AF4–AF8, AF11, AG6, AG7 | console: `QuPath-0.7.0 (console).exe script` |
| Excel (not LibreOffice) | AD3, AD6, AD8, AH5 | — |
| A build from before a feature | P14, Y14, AJ12, AL11, AM12 | check out the commit before the feature |
| ASAP XML fixtures | AI-* | one of each type; decimal-comma twin; `mtd.xml` from an SCN |

### Making a slide read-only

Several tests need a scene whose annotations load but cannot be edited. Use either way:

1. **Second instance.** Open the same slide and scene in a second viewer. The second one is locked out
   (see AE1).
2. **Slide-ID mismatch.** Back up the annotation file, change its `slideId` to any other value, reopen
   the slide and choose **Open Read-Only**. Restore the backup afterwards.

The drawing tools cannot be armed on a read-only slide. Every route to that state also switches to
Navigate, so a check that needs "a drawing tool held over a read-only slide" cannot be set up. AB19 and
AB23 were struck for this reason.

---

## Smoke test (about 15 minutes)

Run these after any change, before a targeted group: OPEN-1, NAV-1, NAV-4, CLOSE-1, FND-3, P1, P10, Y4,
AB1, AB6, AJ2, AL2, AL5, U1, U3, AA3, Z3, V5, V7, W1, AC5, AC15, AM2, AM3, AM4, PS-1, Item0-6, AE1, AD4,
AF3, AG3, AH3, AK2, CM-1.

---

## 1. Slides, windows and navigation

Sources: `MainWindow.cpp`, `main.cpp`, CLAUDE.md. None of these areas had a check group.

### Opening slides and Recent Files

- [ ] **OPEN-1 Open from the dialog.** Press Ctrl+O and pick an SVS. **Expect:** the slide renders. The
  title reads `SlideIO Viewer - <path>`. Close and the three exports become enabled. The viewport has
  focus, so V/R work without a click.
- [ ] **OPEN-2 Open a DICOM folder.** Press Ctrl+Shift+O (present only when the DCM driver is built) and
  pick a study folder. Then pick a folder with no DICOM files. **Expect:** the study opens. The empty
  folder reports the driver's "no valid DICOM files found", not "cannot find a driver".
- [ ] **OPEN-3 Command line.** Run `slideio-viewer.exe "C:\path\slide.svs"`. **Expect:** it opens and is
  added to Recent Files.
- [ ] **OPEN-4 Drag and drop.** Drop a slide on the window, then a `.txt`. **Expect:** the slide opens.
  The `.txt` shows a no-drop cursor and nothing happens.
- [ ] **OPEN-5 Failed open.** Open a corrupt or unsupported file. **Expect:** an error box appears and
  the title goes back to plain `SlideIO Viewer`. Recent Files does not list the file, and the entry that
  was at the top is still there (issue #5).
- [ ] **OPEN-6 Recent Files list.** Open 11 different slides. **Expect:** at most 10 entries, newest
  first, labelled `&N  <name>`, with the full path in the status tip. Clicking an entry reopens it.
- [ ] **OPEN-7 One entry per file (Windows).** Open the same slide from the command line (backslashes)
  and from the dialog (forward slashes). **Expect:** it is listed once, at the top.
- [ ] **OPEN-8 DICOM folder entry.** **Expect:** the entry is labelled with the folder name, not blank.
- [ ] **OPEN-9 Clear Recent Files.** **Expect:** a single disabled `(No recent files)` item, which
  survives a restart.

### Close and exit

- [ ] **CLOSE-1 Ctrl+W.** **Expect:** the viewport is empty and the title is `SlideIO Viewer` with no
  path. Close and the exports are disabled. Navigate is the active tool. The Scenes and Associated Images
  panels are empty. Dock visibility is unchanged. On a Z-stack or time series the Z/T sliders are gone
  too (issue #7).
- [ ] **CLOSE-2 Close, then reopen the same file.** Reopen it from Recent Files. **Expect:** the Scenes
  and Associated Images panels fill again.
- [ ] **CLOSE-3 Window and dock state.** Move or resize the window, rearrange or float the docks, then
  restart. **Expect:** the same geometry and layout.
- [ ] **CLOSE-4 A pane turned on comes to the front.** With several panes tabbed together, turn another
  one on from the View menu or its shortcut (e.g. Ctrl+Shift+D). **Expect:** its tab is added and
  selected, so its contents are what shows. After a restart, the tab that was in front is in front again.

### Navigation

- [ ] **NAV-1 Wheel zoom.** **Expect:** zoom centres on the cursor. The status-bar magnification, the
  zoom indicator and the minimap rectangle all update.
- [ ] **NAV-2 Keyboard.** Press the arrow keys, `+`, `=` and `-` with the viewport focused. **Expect:**
  the arrows pan by a fixed step. `+`/`=` zoom in and `-` zooms out.
- [ ] **NAV-3 View zoom actions.** Press Ctrl++, Ctrl+-, Ctrl+0 and Ctrl+1. **Expect:** zoom in and out.
  Ctrl+0 fits the whole slide. Ctrl+1 shows one screen pixel per slide pixel.
- [ ] **NAV-4 Minimap.** Press Ctrl+M twice, then click a spot on the minimap. **Expect:** the minimap
  hides and shows. The click recentres the viewport there.
- [ ] **NAV-5 Zoom slider.** Drag the zoom indicator. **Expect:** zoom changes around the view centre
  and the label follows.
- [ ] **NAV-6 Full screen.** Press F11 twice. **Expect:** full screen with the menu item checked, then
  back. Enter full screen again, arm the Rectangle tool, click the slide and press Esc twice. **Expect:**
  the first Esc returns to Navigate and stays full screen; the second leaves full screen and unchecks the
  menu item (issue #6). In full screen, open the File and View menus and File > Open... **Expect:** each
  menu and the dialog appear above the slide. Enter full screen and close the window with Alt+F4, then start the viewer again.
  **Expect:** it opens in a normal window at its previous size, with the menu item unchecked and the
  menus working.
- [ ] **NAV-7 Cursor read-out.** **Expect:** the status bar shows slide coordinates that update live.
- [ ] **NAV-8 Z/T controls.** Use a Z-stack or time series. **Expect:** the plane or frame changes. On a
  2-D slide the controls are absent or inert.

### Scenes and associated images

- [ ] **SCN-1 Scenes panel.** Use a multi-scene file. Press Ctrl+Shift+T and click scene 2.
  **Expect:** one thumbnail per scene. Scene 2 opens and is marked active. The panel does not rebuild or
  flash.
- [ ] **SCN-2 Single-scene slide.** **Expect:** one thumbnail, not an empty panel.
- [ ] **SCN-3 Associated image.** Press Ctrl+Shift+A and click the label. **Expect:** a separate
  resizable window at native resolution. The main slide and the Metadata panel do not change.
- [ ] **SCN-4 No associated images.** **Expect:** the panel is empty.

### Preferences, folders, About

- [ ] **PREF-1 Contents.** Open Tools > Preferences. **Expect:** the annotation workspace with Browse,
  "Your name" with a "Required before creating annotations" placeholder, the annotation categories (one
  per line), and a note that saved annotations stay where they are.
- [ ] **PREF-2 Name required.** Clear the name, open a slide and press R. **Expect:** "Your name is
  needed", then Preferences. Cancelling returns to Navigate.
- [ ] **PREF-3 Open Annotations Folder.** Try it on a fresh workspace, then on one with saves.
  **Expect:** "No annotations yet" naming the future path, then the folder in Explorer.
- [ ] **PREF-4 Open Log File.** **Expect:** the log opens, or an info box says it does not exist yet.
- [ ] **PREF-5 About.** **Expect:** version, system and licence information. The version matches the
  release tag.
- [ ] **PREF-6 Online help.** Choose Help > SlideIO Viewer Help, then press F1. **Expect:** each opens
  `https://slideio-doc.readthedocs.io/en/latest/viewer/index.html` in the default browser.

## 2. Panels and display

### Metadata (`2026-05-10-metadata-dock-pane-design.md`)

- [ ] **MD-1 Toggle.** Press Ctrl+Shift+D. **Expect:** Property/Value columns. Slide and Scene are
  expanded; nested items are collapsed.
- [ ] **MD-2 Null metadata.** **Expect:** Slide shows `(no metadata)` with no children.
- [ ] **MD-3 Rich metadata (CZI).** **Expect:** `{N keys}`, `[N items]` with `[0]`, `[1]`... children,
  and `(null)`. Nothing is truncated and the UI stays responsive.
- [ ] **MD-3a Channel numbering** (issue #12). Expand Channels on a multi-channel CZI. **Expect:** they
  read `Channel 1: <name>`, `Channel 2: <name>`..., in the same order and with the same names as the
  Channels pane. On a slide with unnamed channels both panes read `Channel 1`, `Channel 2`...
- [ ] **MD-4 Scene switch.** **Expect:** the Scene subtree updates. An associated-image preview does not
  change it.
- [ ] **MD-5 Copy as text.** **Expect:** the whole tree, including collapsed nodes, as `Property: Value`
  lines indented two spaces per level.
- [ ] **MD-6 Close and persist.** Press Ctrl+W, then dock or float the panel and restart. **Expect:**
  the panel is empty after close, and its position survives the restart.

### Channels (`2026-05-17-channels-pane-improvements-design.md`)

- [ ] **CH-1 Fluorescence histograms.** Expand a row and drag the min/max handles. **Expect:** the
  histogram is in the channel colour. Min/Max update while dragging; the viewport updates on release.
- [ ] **CH-2 RGB brightfield.** **Expect:** R, G and B histograms. The change is visible on release.
- [ ] **CH-2a RGB brightfield opens native** (issue #9). Open an H&E SVS whose channels do not span
  0..255 (`S1303802-11-HE-DX1.svs`: detected R 38, G 21, B 31). **Expect:** Min/Max read 0..255 on
  every channel, and the colours match another viewer's. Auto stretches each channel to what was
  detected; Reset puts it back to 0..255. The Scenes thumbnail and the minimap show the native colours
  too.
- [ ] **CH-2b One-channel slides** (issue #8). Open a Gray8 and a Gray16 TIFF, and a one-channel
  fluorescence slide. **Expect:** on open, Min/Max and the histogram handles show the range the image is
  drawn at, not 0..255, and Auto is enabled. Typing Min = 100 / Max = 150, dragging a handle, Reset and
  Auto each change the image. CH-2a still opens at 0..255.
- [ ] **CH-3 Master tri-state.** Use 4 channels. Untick one; click master; click master again.
  **Expect:** partial, then all off, then the previous partial set restored. The "N visible" count is
  right at each step.
- [ ] **CH-4 Min ≥ Max.** Type Min above Max and press Tab. **Expect:** Min snaps to Max − 1 with no
  popup.
- [ ] **CH-5 Auto / Reset / Log-Lin.** **Expect:** Auto restores the detected range and Reset gives the
  full type range. Log/Lin switches the Y scale (log is the default).
- [ ] **CH-6 Unavailable / scene / Z.** **Expect:** "Histogram unavailable" when the block read fails,
  with the controls still working. Rows rebuild on a scene switch. Changing Z does not recompute.

### Scale bar (`2026-05-25-scale-bar-design.md`, calibration spec)

- [ ] **SB-1 Calibrated slide.** **Expect:** a capped bar above the zoom slider, labelled with a round
  1/2/5 value, always in µm.
- [ ] **SB-2 Pan.** **Expect:** the bar and label are unchanged.
- [ ] **SB-3 Zoom range.** **Expect:** the bar snaps through 1/2/5 values and stays inside the groove.
- [ ] **SB-4 Uncalibrated slide.** **Expect:** `Uncalibrated` with a warning icon in place of the bar.
  Zoom still works.
- [ ] **SB-5 Close, then open another.** **Expect:** the bar goes, then comes back at the right length.

### Colour management (`2026-10-06-color-management-design.md`)

- [ ] **CM-1 Embedded profile.** Toggle View > Color Management (Ctrl+Shift+I). **Expect:** the image
  changes visibly. The status bar shows `sRGB` when on. Properties > Source follows. The minimap and
  thumbnails do not change.
- [ ] **CM-2 No profile, no default.** **Expect:** the item is greyed with "This slide embeds no ICC
  profile, and no default profile is set".
- [ ] **CM-3 Set Default ICC Profile.** Set it, then reopen a profile-less slide. **Expect:** enabled,
  and Source reads Supplied / "Default setting". Only slides opened afterwards are affected.
- [ ] **CM-4 Bad default.** Pick a non-ICC file, then a CMYK `.icc`. **Expect:** both are rejected with
  the reason, and the setting is unchanged.
- [ ] **CM-5 Clear default.** **Expect:** enabled only when a default is set. Afterwards a profile-less
  slide has no profile.
- [ ] **CM-6 Fluorescence / CZI.** **Expect:** greyed. Fluorescence says "applies to RGB brightfield
  slides only"; a CZI gets the no-profile tooltip.
- [ ] **CM-7 Bind failure.** Corrupt the default `.icc` on disk and restart. **Expect:** greyed, with
  "could not be used" and SlideIO's message in the tooltip and the log.
- [ ] **CM-8 Persistence.** Turn it on and restart. **Expect:** still on.

### Per-slide ICC profile (`2026-10-07-per-slide-color-profile-design.md`)

- [ ] **ICC-1 Override a profile-less slide.** Use View > Set ICC Profile for This Slide. **Expect:** no
  warning. Colour management turns on, the scene reopens, and Origin reads "Per-slide override".
- [ ] **ICC-2 Displacing an embedded profile.** **Expect:** "This slide embeds its own color
  profile (name)... Continue?". Cancel changes nothing. Continue applies it with a "displaced: <name>"
  note.
- [ ] **ICC-3 Per slide, persists.** **Expect:** it applies after a restart, applies to every scene of
  that file, and does not apply to another slide.
- [ ] **ICC-4 Clear.** **Expect:** enabled only with an override. The slide falls back to embedded or
  default, and colour management stays on.
- [ ] **ICC-5 Manage dialog.** Remove one row, then Remove All. **Expect:** status OK / File missing /
  Not a profile, with a displaced marker. An open slide whose entry was removed reopens without it.
- [ ] **ICC-6 Stale override.** Delete the `.icc`, then reopen. **Expect:** the slide falls back and the
  problem is shown. The entry is kept and shown as "File missing".
- [ ] **ICC-7 Gated.** Use a fluorescence slide. **Expect:** Set ICC Profile is disabled, with the
  reason as a tooltip.
- [ ] **ICC-8 Format regression.** Open SVS, NDPI, CZI, a DICOM folder, a multi-scene VSI and a PNG.
  **Expect:** all open normally.

### Disabled-item explanations (`2026-10-08-help-system-design.md`)

Most of this spec is deferred: there is no help window, and F1 and Ctrl+/ do nothing.

- [ ] **HLP-1 Tooltip in the menu.** Hover the greyed Color Management item. **Expect:** the reason
  tooltip. The status tip shows only while the item is unavailable.
- [ ] **HLP-2 Stale reason cleared.** Use a fluorescence slide, then Ctrl+W, then hover Set ICC Profile.
  **Expect:** the old reason is gone.
- [ ] **HLP-3 Known gap.** Hover Clear ICC Profile, Clear Default and File > Close while disabled.
  **Expect:** no reason today. Record what you see.

## 3. Annotations: drawing, persistence, undo

### Foundation (`2026-10-08-annotation-foundation-design.md`)

- [ ] **FND-1 Navigate pans.** Under V, left-drag. **Expect:** a pan, with nothing created or
  selected.
- [ ] **FND-2 Space and middle button pan under any tool.** Under R, hold Space long enough to
  auto-repeat. **Expect:** both pan and nothing is drawn.
- [ ] **FND-3 Click selects; a zero-area drag creates nothing.** **Expect:** a click on a shape selects
  it and a click on empty space clears the selection. A Rectangle click without a drag creates nothing.
- [ ] **FND-4 A drag keeps its owner.** Release outside the viewport, and switch tools mid-drag.
  **Expect:** the drag completes cleanly.
- [ ] **FND-5 Escape mid-rectangle.** **Expect:** nothing is created.
- [ ] **FND-6 No smear on zoom.** Wheel quickly. **Expect:** the shapes stay crisp and placed, and the
  selected shape draws on top.
- [ ] **FND-7 No slide.** **Expect:** the drawing tools are greyed and Navigate is active.

### Persistence (`2026-10-10-annotation-persistence-design.md`, followups item 0)

- [ ] **Item0-1 ICC change.** Draw, then change or clear the slide profile. **Expect:** every annotation
  is still present.
- [ ] **Item0-2 Broken file.** Truncate the JSON, hash it, then reopen, draw, wait and close.
  **Expect:** a dialog names the file. The tools are disabled and the log says "file left unchanged".
  The file is byte-identical.
- [ ] **Item0-3 Slide-ID mismatch.** Try each answer: Read-Only, Ignore Them, Use for This Slide (then
  close without drawing). Also try Esc and the title-bar X. **Expect:** Read-Only loads with no edits and
  no writes. Ignore shows an empty model with the file untouched. Use rewrites the stored id. Esc and X
  act as Read-Only. Each answer logs a line.
- [ ] **Item0-4 Associated image.** **Expect:** every drawing tool is disabled.
- [ ] **Item0-5 Read-only refuses drag and Delete.** **Expect:** nothing changes, the status bar
  explains, and nothing is written.
- [ ] **Item0-6 Quit without closing.** Draw, then quit within 2 s. **Expect:** the annotation is there
  after relaunch.
- [ ] **PS-1 Never-annotated slide.** **Expect:** no dialog and tools enabled. An `annotations/` folder
  appears only with the lock (AE10) or the first save.
- [ ] **PS-2 Future `schemaVersion`.** Set it to 3. **Expect:** "written by a newer version". Tools are
  disabled and the file is unchanged.
- [ ] **PS-3 Unreadable file.** Deny read on the file. **Expect:** the OS error is shown, tools are
  disabled and nothing is written.
- [ ] **PS-4 Unwritable workspace.** **Expect:** a dialog names the path and the save is held. There is
  no fallback folder and one warn line, not one per retry. After unblocking, the next autosave succeeds
  and logs "recovered".
- [ ] **PS-5 Save failure on close.** **Expect:** Retry, Choose Another Folder, and Discard and Close
  (stating the count). Esc means Retry, retries stop at five, and Discard logs at error.
- [ ] **PS-6 Persistent "Annotations disabled".** Wait more than 8 s on a read-only slide. **Expect:**
  the amber label stays with the reason as a tooltip. Close clears it.
- [ ] **PS-7 Author.** **Expect:** no prompt at startup; the prompt comes on the first draw. `author` is
  stamped, and a colleague's `author` is never rewritten.
- [ ] **PS-8 Scenes kept apart.** Draw on scene 0 and scene 1 and switch back and forth. **Expect:**
  separate `.s0`/`.s1` files, and no shape crosses scenes.

### Polygon and freehand line (followups item 0, group P)

- [ ] **P1** Make a triangle with 2 clicks and a double-click. **Expect:** exactly 3 vertices in the
  JSON.
- [ ] **P2** Drag a polygon. **Expect:** all vertices move by the same delta; it does not become a box.
- [ ] **P3** Click the notch of a concave polygon, then its solid part. **Expect:** the notch misses and
  the solid part hits. The selection outline follows the edges.
- [ ] **P4** Run P1–P11 on a fresh slide, then close and reopen twice. **Expect:** identical shapes, and
  the loaded shapes are draggable.
- [ ] **P5** Alt-Tab mid-trace. **Expect:** the trace and its rubber band are intact.
- [ ] **P6** Draw a freehand line (F). **Expect:** open, unfilled, saved as `polyline`.
- [ ] **P7** Place vertices with slow clicks. **Expect:** vertex count equals click count.
- [ ] **P8** Press R mid trace-drag. **Expect:** the selection does not change.
- [ ] **P9** Press Escape mid-trace, then Backspace repeatedly. **Expect:** Escape discards. Each
  Backspace drops one vertex, and going past the first ends the trace cleanly.
- [ ] **P10** Close by clicking vertex 1, by double-click, by Enter and by keypad Enter. **Expect:** all
  commit, and the orange close dot appears within about 6 px.
- [ ] **P11** Trace fast with the button held. **Expect:** smooth, and the vertex count follows path
  length, not duration.
- [ ] **P12** Use a read-only slide. **Expect:** Polygon and Freehand cannot be selected; the checkmark
  stays on Navigate.
- [ ] **P13** **Expect:** the hint "Click to add points, double-click or Enter to finish, Esc to cancel"
  for both tools.
- [ ] **P14** Use a pre-polygon build on a polygon file. **Expect:** `unknown geometry type 'polygon'`,
  and the files are byte-identical.
- [ ] **P15** Press a tool key mid move-drag. **Expect:** the move completes and nothing else happens.
- [ ] **P16** Press V with a half-traced polygon, without moving. **Expect:** the preview disappears at
  once.
- [ ] **P17** Click once and double-click, or press Enter at 2 vertices. **Expect:** the trace stays
  alive.
- [ ] **P18 No duplicate closing vertex** (geometry-widening §6.11). Trace with the button held, stop,
  and double-click. **Expect:** Area is a number, not "self-intersecting". Old files measure correctly
  without being rewritten.

### Ellipse and drag modifiers (`2026-10-16-annotation-ellipse-design.md`, group Y)

- [ ] **Y1** Press E. **Expect:** Ellipse is active and checked.
- [ ] **Y2** **Expect:** the commit matches the preview.
- [ ] **Y3** Drag in reverse. **Expect:** a correct ellipse.
- [ ] **Y4** Hold Shift. **Expect:** a circle.
- [ ] **Y5** Hold Alt. **Expect:** the shape grows from the press point.
- [ ] **Y6** Hold Shift+Alt. **Expect:** a centred circle.
- [ ] **Y7** Rectangle with Shift. **Expect:** a square.
- [ ] **Y8** Click inside, then in a bounding-box corner outside the curve. **Expect:** inside hits, the
  corner misses.
- [ ] **Y9** **Expect:** area and perimeter, with no self-intersection warning.
- [ ] **Y10** Move it. **Expect:** it stays an ellipse.
- [ ] **Y11** Undo, then redo. **Expect:** it goes, then comes back.
- [ ] **Y12** Save, close and reopen. **Expect:** unchanged.
- [ ] **Y13** Make a tiny drag. **Expect:** nothing is created.
- [ ] **Y14** Use a pre-ellipse build. **Expect:** it is refused by name, and the file is
  byte-identical.
- [ ] **Y15** Zoom deep into a big ellipse. **Expect:** smooth, with no facets.
- [ ] **Y16** Release Shift before the button, or press Shift after the drag stops. **Expect:** the
  commit equals the last preview.
- [ ] **Y17** Make a tiny Alt drag. **Expect:** nothing is created.
- [ ] **Y18** Release Alt and the button in either order. **Expect:** the shape is created and selected,
  and V/R/E/P/F still work.
- [ ] **Y19** Alt-Tab mid-drag, return, click, then Ctrl+Z. **Expect:** the shape is created, and Ctrl+Z
  removes it.

### Point marker and arrow (`2026-10-17-annotation-point-arrow-design.md`, group AB)

- [ ] **AB1** Click with Point (M). **Expect:** a marker appears and the tool stays active.
- [ ] **AB2** Place 10 markers fast. **Expect:** 10 in the list; none lost or doubled.
- [ ] **AB3** Zoom from fit to max. **Expect:** the marker stays about 10 px.
- [ ] **AB4** Click the marker, then 10 px away. **Expect:** it hits, then misses (6 px radius).
- [ ] **AB5** Drag it, then Ctrl+Z. **Expect:** it moves, and one undo restores it.
- [ ] **AB6** Drag with Arrow (A). **Expect:** tail at the press, head at the release.
- [ ] **AB7** Drag right-to-left and bottom-to-top. **Expect:** the head is still at the release.
- [ ] **AB8** Click with Arrow, no drag. **Expect:** nothing is created.
- [ ] **AB9** Zoom out on a long arrow. **Expect:** the head shrinks and is never more than half the
  shaft.
- [ ] **AB10** Draw a very short arrow. **Expect:** a proportionate head.
- [ ] **AB11** Click the shaft midpoint, then well off it. **Expect:** it hits, then misses.
- [ ] **AB12** **Expect:** no resize handles on either shape.
- [ ] **AB13** Double-click each. **Expect:** no vertex editing.
- [ ] **AB14** Look at a marker's properties. **Expect:** Point, length 0, no area.
- [ ] **AB15** Look at an arrow's properties. **Expect:** Arrow, a "Length" row (not "Perimeter"), no
  area.
- [ ] **AB16** Use Show annotation on a marker. **Expect:** it recentres without changing zoom.
- [ ] **AB17** Save, close and reopen. **Expect:** both return, and the arrow keeps its direction.
- [ ] **AB18** **Expect:** `schemaVersion` 1 when no layers are used.
- [ ] **AB20** Press Escape under Point. **Expect:** Navigate becomes active.
- [ ] **AB21** **Expect:** markers are visible on pale background and on dark tissue.
- [ ] **AB22** Delete, Ctrl+Z, Ctrl+Y. **Expect:** one step each.
- [ ] **AB-M Mnemonic.** **Expect:** in the Tools menu, Point Marker's underlined letter is M, not P.

### Text (`2026-10-23-annotation-text-design.md`, group AJ)

- [ ] **AJ1** Press T and click. Then try the same in a read-only window. **Expect:** an editor opens.
  In the read-only window Text is disabled and T does nothing.
- [ ] **AJ2** Type "Invasive carcinoma" and press Enter. **Expect:** the words are upright, in the
  annotation colour, on a plate. There is a Text row in the list, and the file stores `type: text`.
- [ ] **AJ3** Zoom from 1× to 40× at the anchor. **Expect:** the words keep their screen size and stay on
  the tissue.
- [ ] **AJ4** Type and press Escape; press Enter with nothing typed. **Expect:** nothing is created.
- [ ] **AJ5** Type, then click elsewhere, change tool, or press Ctrl+W, and reopen. **Expect:** the words
  are kept in every case.
- [ ] **AJ6** Create, Undo, Redo. **Expect:** one step each way, with the words intact.
- [ ] **AJ7** Double-click to edit, press Enter, then Undo; then clear the words and press Enter.
  **Expect:** a one-step revert. Empty words keep the old words.
- [ ] **AJ8** Drag the plate; overlap it with older and newer shapes. **Expect:** one undo step. The
  stacking decides which shape is hit.
- [ ] **AJ9** Clear the label in the properties panel. **Expect:** a dot and a grey italic `(empty)`; it
  can still be selected and deleted.
- [ ] **AJ10** Classify a text as Tumor. **Expect:** the strip and Export Counts exclude it.
- [ ] **AJ11** Export to GeoJSON and CSV, then re-import the GeoJSON. **Expect:** a Point with
  `slideio.type` `text`, a CSV row with empty measurements, and the text comes back intact.
- [ ] **AJ12** Use a pre-text build. **Expect:** `unknown geometry type 'text'`, and the file is
  unchanged.

### Ruler and angle (`2026-10-24-annotation-ruler-angle-design.md`, group AL)

- [ ] **AL1** Press L and draw, then G and draw. Try a read-only window. **Expect:** Ruler and Angle;
  both are disabled when read-only.
- [ ] **AL2** **Expect:** the on-slide label, the panel's Length and the CSV `length_um` agree.
- [ ] **AL3** Set a manual calibration. Then use an uncalibrated slide. **Expect:** the label becomes
  `… (manual)` at once. On the uncalibrated slide it shows px with a warning icon.
- [ ] **AL4** Zoom from 1× to 40×. **Expect:** the label keeps its size and sits beside the line, never
  on it.
- [ ] **AL5** Press, drag, release, then click. **Expect:** a live angle while choosing. The value matches
  the panel's Angle and the CSV `angle_deg`.
- [ ] **AL6** Draw an axis-aligned right angle, then a 45° pixel angle at a 1 × 2 µm/px calibration.
  **Expect:** 90.0°, then about 63.4° (CSV 63.43494882).
- [ ] **AL7** End a pending angle with Escape, a tool change, or Ctrl+Z, and make lone clicks.
  **Expect:** nothing is created.
- [ ] **AL8** Undo/redo a create, move and delete. **Expect:** one step each.
- [ ] **AL9** Classify both as Tumor. **Expect:** the Tumor count does not change.
- [ ] **AL10** GeoJSON round trip. **Expect:** LineStrings tagged `ruler` (2 points) and `angle`
  (3 points) that come back as a ruler and an angle.
- [ ] **AL11** Use a pre-ruler build. **Expect:** it is refused naming `ruler`, and the file is
  byte-identical.

### Undo / redo (`2026-10-13-annotation-undo-redo-design.md`, followups group U)

- [ ] **U1** Draw a polygon, Ctrl+Z, Ctrl+Y. **Expect:** it goes at once without a pan, then returns
  identical.
- [ ] **U2** Undo a delete. **Expect:** it returns, selected.
- [ ] **U3** Undo a move. **Expect:** the coordinates are bit-identical to the JSON.
- [ ] **U4** **Expect:** one drag is one undo step.
- [ ] **U5** First action on a slide: 3 vertices, Ctrl+Z, Enter. **Expect:** one vertex is dropped and
  the trace stays live. Enter commits nothing. Undo stays enabled.
- [ ] **U6** Press Ctrl+Z with the button held mid-drag. **Expect:** nothing; the drag completes as one
  Move.
- [ ] **U7** Escape-cancel a move, then Ctrl+Z. **Expect:** the previous operation is undone.
- [ ] **U8** Edit, switch slides, switch back. **Expect:** Undo is greyed on both.
- [ ] **U9** On a read-only slide. **Expect:** Undo and Redo are greyed, and the file is byte-identical.
- [ ] **U10** Undo, autosave, reopen. **Expect:** the undone state loads.
- [ ] **U11** **Expect:** "Undo Draw/Delete/Move <Shape>", and "Undo Vertex" mid-trace.
- [ ] **U12** Alt-Tab mid-move, return, then Ctrl+Z. **Expect:** only that annotation returns.
- [ ] **U13** Press Ctrl+Z while panning. **Expect:** the last operation is undone.
- [ ] **U14** Press Ctrl+Z in the Metadata filter and in a channel box. **Expect:** only the typing is
  undone.
- [ ] **U15** Draw 3, toggle colour management, then Ctrl+Z. **Expect:** the third is removed.
- [ ] **U16** As U15, but set and clear the slide profile. **Expect:** the same result.
- [ ] **U17** Click with a wobble under 6 px. **Expect:** no move and no undo step.
- [ ] **U18** Make a short deliberate drag, then a drag back to the start. **Expect:** the first is
  recorded; the return records nothing.
- [ ] **U19** Move, autosave, undo, autosave. **Expect:** the original `modifiedAt` is back.
- [ ] **U20** Escape a drag past the threshold with the button down. **Expect:** geometry and
  `modifiedAt` are unchanged, and Undo stays greyed.

## 4. Annotations: editing

### Vertex editing (`2026-10-17-annotation-vertex-editing-design.md`, group Z)

- [ ] **Z1** Double-click a polygon. **Expect:** a handle on every vertex.
- [ ] **Z2** Double-click to finish a trace. **Expect:** the shape closes and the mode is not entered.
- [ ] **Z3** Drag a handle. **Expect:** only that vertex moves.
- [ ] **Z4** Release, then Ctrl+Z. **Expect:** one undo restores it.
- [ ] **Z5** Press on the outline between handles and drag. **Expect:** a vertex is inserted and follows
  the cursor.
- [ ] **Z6** Ctrl+click a handle. **Expect:** that vertex is removed.
- [ ] **Z7** Remove down to 3 on a polygon, then once more. **Expect:** refused, with the minimum named.
- [ ] **Z8** Remove down to 2 on a freehand line, then once more. **Expect:** refused, with a message.
- [ ] **Z9** Escape mid-drag, then Ctrl+Z. **Expect:** the vertex returns. Ctrl+Z undoes the earlier
  edit.
- [ ] **Z10** Press Escape with no drag. **Expect:** the mode ends.
- [ ] **Z11** Click off the shape. **Expect:** the mode ends.
- [ ] **Z12** Use a dense trace at low zoom, then zoom in. **Expect:** the nearest vertex is grabbed,
  and the handles separate.
- [ ] **Z13** Save and reopen. **Expect:** the edits are kept.
- [ ] **Z14** Leave the mode and drag the body. **Expect:** the whole shape moves.
- [ ] **Z15** Double-click a rectangle or ellipse. **Expect:** nothing.
- [ ] **Z16** On a read-only slide. **Expect:** the mode is not entered.
- [ ] **Z17** Hand-edit a duplicated vertex into the file. **Expect:** the grabbed vertex is the one that
  moves.
- [ ] **Z18** Space-drag over a handle, then an ordinary drag. **Expect:** a pan, then a vertex move.
- [ ] **Z19** Alt-Tab mid-drag, then Ctrl+Z twice. **Expect:** the edit survives and undoes in order.
- [ ] **Z20** Press a few px off-centre and move 1 px. **Expect:** no jump.
- [ ] **Z21** Double-click under Rectangle or Ellipse. **Expect:** the mode is not entered; drawing works.
- [ ] **Z22** Select another polygon from the list or by Space-click. **Expect:** the old handles go.
- [ ] **Z23** Press near a vertex just outside the viewport. **Expect:** no invisible grab.
- [ ] **Z24** Press Ctrl+Z mid-drag. **Expect:** the drag stops, and the undo result stands.
- [ ] **Z25** Press a tool key mid-drag, then Ctrl+Z twice. **Expect:** one step.
- [ ] **Z26** **Expect:** the move cursor while dragging.

### Box resize (`2026-10-17-annotation-box-resize-design.md`, group AA)

- [ ] **AA1** Select a rectangle, then an ellipse. **Expect:** eight handles each.
- [ ] **AA2** Select a polygon. **Expect:** no handles; a double-click enters vertex editing.
- [ ] **AA3** Drag the corners. **Expect:** two edges move.
- [ ] **AA4** Drag the edge midpoints. **Expect:** one edge moves.
- [ ] **AA5** Drag the body. **Expect:** a move.
- [ ] **AA6** Drag past the opposite edge. **Expect:** the box flips and stays correct.
- [ ] **AA7** Collapse it to nothing. **Expect:** it comes back 1 screen px thick.
- [ ] **AA8** Resize, then Ctrl+Z. **Expect:** one step.
- [ ] **AA9** Press and release without moving. **Expect:** no step.
- [ ] **AA10** Press Escape mid-resize. **Expect:** the box is restored with no step.
- [ ] **AA11** Alt-Tab mid-resize. **Expect:** the resize survives as one step.
- [ ] **AA12** Press a tool key mid-resize. **Expect:** the resize survives as one step.
- [ ] **AA13** Press Ctrl+Z mid-resize. **Expect:** the drag stops, and the undo result stands.
- [ ] **AA14** Close the slide mid-resize, open another, press Escape. **Expect:** nothing is restored
  across slides.
- [ ] **AA15** Space-drag over a handle. **Expect:** a pan.
- [ ] **AA16** On a read-only slide. **Expect:** refused with a message.
- [ ] **AA17** Reopen. **Expect:** the new size is kept.
- [ ] **AA18** Resize an ellipse. **Expect:** the panel's area and perimeter update.
- [ ] **AA19** **Expect:** the SizeAll cursor.
- [ ] **AA20** Press Ctrl+Z while holding the drag, release, then Ctrl+Z. **Expect:** no fabricated
  Resize step.
- [ ] **AA21** Under Rectangle, start a drag near a handle. **Expect:** a new shape is drawn.
- [ ] **AA22** Make a no-op handle press. **Expect:** no autosave.
- [ ] **AA23** On a read-only slide, drag from a handle. **Expect:** a pan, with an explanation.

### Annotation list and properties (`2026-10-14-annotation-panels-design.md`, group V)

- [ ] **V1** Press Ctrl+Shift+L and Ctrl+Shift+E. **Expect:** the docks toggle.
- [ ] **V2** Restart. **Expect:** the dock state is restored.
- [ ] **V3** **Expect:** the rows match the viewport, with the count in the title.
- [ ] **V4** **Expect:** a new shape's row appears at once.
- [ ] **V5** **Expect:** a row click selects the shape.
- [ ] **V6** **Expect:** a shape click highlights its row.
- [ ] **V7** Filter. **Expect:** non-matching rows are hidden, and the title reads `N of M`.
- [ ] **V8** Sort. **Expect:** each order works, and labels stay with their shapes.
- [ ] **V9** Double-click a row. **Expect:** the view centres on and fits the shape.
- [ ] **V10** Delete from the list, then Ctrl+Z. **Expect:** "Undo Delete <shape>", restored at its
  paint order.
- [ ] **V11** Edit a label and reopen. **Expect:** it is kept.
- [ ] **V12** Change colour, width and opacity. **Expect:** an immediate repaint.
- [ ] **V13** Tab through the fields. **Expect:** no undo step, and `modifiedAt` is unchanged.
- [ ] **V14** Press Ctrl+Z in Notes and in the filter. **Expect:** only the text is undone.
- [ ] **V15** Type a label, then click another row. **Expect:** the label goes to the original shape.
- [ ] **V16** Switch slides. **Expect:** no stale rows or fields.
- [ ] **V17** **Expect:** a viewport selection scrolls its row into view.
- [ ] **V18** **Expect:** after many rebuilds the View menu still reads "Annotation &List", and Alt+V,L
  works.
- [ ] **V19** Type a label, then Close, open another slide, or quit. **Expect:** the label is saved.
- [ ] **V20** *(not yet run)* Hide the dock with an uncommitted edit. **Expect:** the value survives.
- [ ] **V21** *(not yet run)* Drag one of several hundred annotations with both docks open. **Expect:**
  no lag.
- [ ] **V22** On a read-only slide, delete and edit. **Expect:** refused, with no step, and the field
  snaps back.
- [ ] **V23** *(not yet run)* Use Edit > Undo with a pending field edit. **Expect:** safe. Record whether
  the edit was kept.
- [ ] **V24** On a read-only slide. **Expect:** the property fields are disabled.
- [ ] **V25** Switch slides, do a colour-profile reopen, and quit with an autosave pending, all with
  both panels open. **Expect:** no crash. Undo survives the reopen, and the work is saved.
- [ ] **V26** Change classification and colour, then Ctrl+Z. **Expect:** the fields revert without
  stealing the caret.

### Measurement (`2026-10-14-annotation-measurement-design.md`, group W)

- [ ] **W1** Compute a rectangle by hand from the JSON and the resolution. **Expect:** it matches to
  3 significant figures.
- [ ] **W2** Freehand line. **Expect:** Length only.
- [ ] **W3** Polygon. **Expect:** Area and Perimeter.
- [ ] **W4** Figure-eight. **Expect:** `unavailable (self-intersecting)`, with Perimeter shown.
- [ ] **W5** Uncalibrated slide. **Expect:** `px` / `px²`.
- [ ] **W6** Shapes past 1 mm and 1 mm². **Expect:** each row promotes to mm independently.
- [ ] **W7** Select a shape. **Expect:** no undo step, and `modifiedAt` is unchanged.
- [ ] **W8** Drag a long trace with the dock open. **Expect:** no lag.
- [ ] **W9** Reopen. **Expect:** identical values.
- [ ] **W10** **Expect:** 3 significant figures (`974000 µm²`), then `1.00 mm²` near 1 mm².
- [ ] **W11** Select and deselect a closed shape and then an open one. **Expect:** the rows reset to
  Area/Perimeter, empty.
- [ ] **W12** Delete and undo with the dock open. **Expect:** no flicker.
- [ ] **W13** Drag a polygon of hundreds of vertices. **Expect:** Area does not change by a digit.
- [ ] **W14** Drag the W4 shape. **Expect:** still self-intersecting.
- [ ] **W15** Type a label, then Ctrl+W or Alt+F4 with the caret still in the field. **Expect:** the
  label is saved.
- [ ] **W16** **Expect:** the Delete key removes the selection.

### Calibration (`2026-10-24-measurement-calibration-design.md`, group AK)

- [ ] **AK1** Calibrated slide. **Expect:** no badge. Slide Properties reads "metadata, 0.4990 × 0.4990
  µm/px", and the CSV `calibration_source` is `metadata`.
- [ ] **AK2** Set View > Calibration to 0.5 × 0.5. **Expect:** prefilled. Area scales by (0.5/0.499)².
  The badge reads "manual calibration" and the scale bar "(manual)".
- [ ] **AK3** Reopen and restart. **Expect:** the override is kept.
- [ ] **AK4** In a multi-scene file, override scene 0. **Expect:** scene 1 is unchanged.
- [ ] **AK5** Reset. **Expect:** everything is back to the AK1 state.
- [ ] **AK6** Uncalibrated slide. **Expect:** a status-bar prompt, `Uncalibrated` with a warning, and px.
  After an override, µm with the manual badge.
- [ ] **AK7** Square off, X 0.25, Y 0.5. **Expect:** the values match a hand calculation from the
  vertices.
- [ ] **AK8** Enter 250, then 0. **Expect:** "Enter a pixel size between 0.01 and 100 µm per pixel.";
  nothing is stored.
- [ ] **AK9** In a read-only window. **Expect:** the override works, and the annotation file hash is
  unchanged.
- [ ] **AK10** Exports. **Expect:** CSV `manual`, and GeoJSON `calibrationSource: "manual"` with
  `pixelSizeMicrons`.
- [ ] **AK11** Log. **Expect:** one info line per apply and per reset.

## 5. Counting, layers and locking

### Categorized counting (`2026-10-18-annotation-categorized-counting-design.md`, group AC)

- [ ] **AC1** Fresh profile. **Expect:** five default categories.
- [ ] **AC2** Edit the list, reopen Preferences, restart. **Expect:** the edit is kept.
- [ ] **AC3** Enter `  Mitosis  / Lymphocyte / (blank) / MITOSIS /  Tumor  `. **Expect:**
  `Mitosis, Lymphocyte, Tumor`.
- [ ] **AC4** **Expect:** the properties Classification combo and the counting combo list the same
  categories.
- [ ] **AC5** Place 5 markers under a category. **Expect:** all 5 carry it.
- [ ] **AC6** Draw a rectangle and an arrow. **Expect:** both are unclassified.
- [ ] **AC7** Change the category. **Expect:** existing markers keep theirs.
- [ ] **AC8** `(none)`. **Expect:** the marker is unclassified, and an Unclassified row appears.
- [ ] **AC9** **Expect:** the tally updates on each marker. The list following the new marker is
  intended.
- [ ] **AC10** **Expect:** configured categories show at 0.
- [ ] **AC11** Type a new category. **Expect:** it joins the list only on the first marker.
- [ ] **AC12** **Expect:** it is then in Preferences and the properties combo.
- [ ] **AC13** Undo that marker. **Expect:** the category stays, at 0.
- [ ] **AC14** Filter the list. **Expect:** the tally does not change.
- [ ] **AC15** Rename one category onto another. **Expect:** the rows merge (5+3 = 8).
- [ ] **AC16** Undo and redo the rename. **Expect:** one step each.
- [ ] **AC17** Rename to empty. **Expect:** refused, with the reason.
- [ ] **AC18** Rename the Unclassified row, or rename a category to "Unclassified". **Expect:** no
  Rename on that row, and the second is refused.
- [ ] **AC19** Remove from list on a category in use. **Expect:** the menu gives the count, and the
  annotations remain.
- [ ] **AC20** Ctrl+W and reopen from Recent Files. **Expect:** everything returns; `schemaVersion` 1.
- [ ] **AC21** Switch slides, then restart. **Expect:** the category is kept across the switch and is
  `(none)` after the restart.
- [ ] **AC22** Read-only slide. **Expect:** the combo is disabled, and the tally is still correct.
- [ ] **AC23** Place 60 markers and undo 54. **Expect:** 10 remain, and the tally matches the list.
- [ ] **AC24** Rename, then remove, the current category. **Expect:** counting continues under the new
  name, and the category stays current after removal.
- [ ] **AC25** Clear the list and restart (registry backend). **Expect:** it stays empty; the defaults
  do not return.
- [ ] **AC26** With the combo closed, press Down/Up and use the wheel. **Expect:** the category does not
  change.
- [ ] **AC27** Type `tumor` against `Tumor`. **Expect:** it corrects to Tumor with no duplicate.
- [ ] **AC28** Rename, then Ctrl+Z only. **Expect:** the annotations revert, and the list stays renamed
  (intended).
- [ ] **AC29** Right-click a tally row. **Expect:** the menu is for the row under the cursor.
- [ ] **AC30** **Expect:** the refusal messages are legible.
- [ ] **AC31** Rename with no slide open. **Expect:** accepted, with no read-only claim.

### Counts export (`2026-10-20-annotation-count-export-design.md`, group AD)

- [ ] **AD1 / AD2** **Expect:** Export Counts is enabled only while a slide is open.
- [ ] **AD3** Open the file in Excel. **Expect:** 7 columns, one row per tally row.
- [ ] **AD4** **Expect:** the same rows in the same order as the strip, Unclassified last.
- [ ] **AD5** **Expect:** the sum equals the dock's `Annotations (N)`, excluding texts, rulers and
  angles.
- [ ] **AD6** Greek and Cyrillic in Excel. **Expect:** no mojibake.
- [ ] **AD7** `Tumor, invasive`. **Expect:** one cell.
- [ ] **AD8** `=1+1` in Excel. **Expect:** shown as text.
- [ ] **AD9** **Expect:** the filter is ignored.
- [ ] **AD10** Empty scene. **Expect:** configured categories at 0 and no Unclassified row.
- [ ] **AD11** Read-only slide. **Expect:** the export works.
- [ ] **AD12** Cancel. **Expect:** no file and no message.
- [ ] **AD13** Overwrite. **Expect:** the dialog asks, and the file is complete.
- [ ] **AD14** Deny-write ACL on an existing CSV. **Expect:** a warning with the path and reason; no
  partial file.
- [ ] **AD15** **Expect:** `<slide>.s<N>.counts.csv` in the workspace.
- [ ] **AD16** Scene 1. **Expect:** `scene` = 1, with that scene's counts.
- [ ] **AD17** **Expect:** a status-bar confirmation naming the file.

### Layers (`2026-10-24-annotation-layers-design.md`, group AM)

- [ ] **AM1** **Expect:** Diagnostic, Teaching and Research, all annotations on Diagnostic, and the file
  hash unchanged until an edit.
- [ ] **AM2** Draw on Teaching. **Expect:** `Layer: Teaching`, `layerId` `teaching`, and the file
  becomes version 2.
- [ ] **AM3** Hide Teaching. **Expect:** its shapes leave the slide and the list, and clicks miss. The
  tally still counts them, with "Includes n on hidden layers".
- [ ] **AM4** Lock Diagnostic. **Expect:** `(locked)`. Move, resize, delete, edits and drawing are all
  refused.
- [ ] **AM5** Set opacity, including by a track click. **Expect:** the shapes fade, and the stored
  colour is unchanged.
- [ ] **AM6** Move to Layer from the row menu and from Edit, then undo and redo. **Expect:** one step
  each.
- [ ] **AM7** Reorder. **Expect:** the top layer paints on top and is hit first.
- [ ] **AM8** New, rename, merge (with undo and redo), delete. **Expect:** Merge is one step. Delete is
  enabled only on an empty layer.
- [ ] **AM9** Relaunch. **Expect:** order, visibility, lock and assignment come back, with no rewrite.
- [ ] **AM10** Read-only window. **Expect:** only visibility works, and the file is unchanged.
- [ ] **AM11** Export with a hidden layer. **Expect:** both CSVs include it, and GeoJSON carries
  `slideio.layer`.
- [ ] **AM12** Use the pre-layers build. **Expect:** a version-2 file is refused unchanged. A version-1
  file written by this build opens.

### File locking (`2026-10-20-annotation-file-locking-design.md`, group AE: two instances)

- [ ] **AE1** A then B on the same scene. **Expect:** B shows the annotations with its tools disabled.
- [ ] **AE2** **Expect:** B's status bar and tooltip name the holder (app, host, pid).
- [ ] **AE3** **Expect:** B's log names the lock file and the holder.
- [ ] **AE4** **Expect:** a `.lock` beside the JSON.
- [ ] **AE5** A closes. **Expect:** the `.lock` disappears.
- [ ] **AE6** B reopens. **Expect:** B can annotate, and the lock names B's pid.
- [ ] **AE7** B tries M, R and P. **Expect:** nothing is drawn, and the hash is unchanged.
- [ ] **AE8** A on scene 0, B on scene 1. **Expect:** both can edit, under separate locks.
- [ ] **AE9** Kill A, open in B. **Expect:** the stale lock is recognised, and B can edit.
- [ ] **AE10** Never-annotated slide. **Expect:** an `annotations/` folder with a `.lock` and no `.json`.
- [ ] **AE11** B changes its workspace. **Expect:** B can edit; only "Switch without copying" is offered.
- [ ] **AE12** A changes its workspace. **Expect:** the lock moves to the new root.
- [ ] **AE13** Malformed JSON. **Expect:** the "could not be used" message, not the lock message, and no
  `.lock` is left.
- [ ] **AE14** Associated image. **Expect:** no lock.
- [ ] **AE15** Export Counts in B. **Expect:** it works.
- [ ] **AE16** Hand-plant a lock naming another host, then delete it. **Expect:** B is read-only, then
  gets the lock after the delete.
- [ ] **AE17** B switches its workspace; then use A's Preferences > OK and Open Annotations Folder.
  **Expect:** A keeps its own folder.

### Workspace change (`2026-10-15-workspace-change-design.md`, group X)

- [ ] **X1** Copy into an empty folder. **Expect:** the annotations are written there and the old file is
  unchanged.
- [ ] **X2** Merge. **Expect:** the receipt "Merged: n added, n superseded, n kept." matches the disk,
  and z-order is kept.
- [ ] **X3** Keep what's on screen. **Expect:** the path and count are named first, and the file holds
  exactly the screen.
- [ ] **X4** Use the new folder's. **Expect:** both files are untouched.
- [ ] **X5** Cancel, then edit. **Expect:** nothing changes, and the edit autosaves.
- [ ] **X6** Corrupt file in the new folder. **Expect:** only "Use the new folder's" and Cancel are
  offered, and no write happens.
- [ ] **X7** Read-only slide. **Expect:** no write is offered.
- [ ] **X8** No slide. **Expect:** no dialog.
- [ ] **X9** **Expect:** no tile flicker or reload.
- [ ] **X10** Two scenes annotated. **Expect:** only the open scene travels, as the dialog says.
- [ ] **X11** **Expect:** Undo is disabled after a switch.
- [ ] **X12** Refused switch. **Expect:** the slide becomes read-only, with a message.
- [ ] **X13** Associated image open. **Expect:** as X1, and the preview is untouched.
- [ ] **X14** Both folders deny writes. **Expect:** a critical dialog, and the annotations stay on screen.
- [ ] **X15** Cancel after a failed flush. **Expect:** the screen is unchanged, including unsaved work.
- [ ] **X16** Press Enter on the refusal. **Expect:** Cancel.
- [ ] **X17** Switch while a slide is opening. **Expect:** "a slide is still opening", and nothing is
  written.
- [ ] **X18** Change the workspace in Preferences (persistence §4.1). **Expect:** the dialog says files
  are not moved.

## 6. Import and export

### GeoJSON export (`2026-10-21-annotation-geojson-export-design.md`, group AF)

- [ ] **AF1** **Expect:** enabled only while a slide is open.
- [ ] **AF2** **Expect:** `<slide>.s<N>.geojson` in the workspace.
- [ ] **AF3** Philips-1 scene 0. **Expect:** 27 features, with ids and order as in the file.
- [ ] **AF4** Import into QuPath (console and GUI). **Expect:** 27 objects, exactly placed.
- [ ] **AF5** **Expect:** Greek, Cyrillic and comma classes byte-identical. Unclassified shapes have no
  class, and colours are kept.
- [ ] **AF6** Ellipse and polygon. **Expect:** areas in QuPath; the ellipse's bounds equal its stored box.
- [ ] **AF7** Freehand and arrow. **Expect:** lines, with the arrow's tail first.
- [ ] **AF8** SCN scene 1. **Expect:** the shapes land on scene 1's tissue in QuPath.
- [ ] **AF9** **Expect:** the filter is ignored.
- [ ] **AF10** **Expect:** a read-only slide exports.
- [ ] **AF11** Empty scene. **Expect:** a valid empty FeatureCollection that QuPath accepts.
- [ ] **AF12** Cancel. **Expect:** nothing.
- [ ] **AF13** Overwrite. **Expect:** the dialog asks, and the file is complete.
- [ ] **AF14** Write-denied file. **Expect:** a warning; the file is untouched.
- [ ] **AF15** **Expect:** a status-bar confirmation.
- [ ] **EXP-1 Unloaded scene refused** (CLAUDE.md). On a scene whose annotation file failed to load, try
  both exports. **Expect:** refused with a warning. No empty FeatureCollection and no zero counts are
  written.

### GeoJSON import (`2026-10-22-annotation-geojson-import-design.md`, group AG)

- [ ] **AG1** **Expect:** Import is disabled with no slide and on read-only scenes, and enabled on an
  editable one. The exports stay enabled when read-only.
- [ ] **AG2** Export, empty the scene, import. **Expect:** "27 will be added", and everything comes back
  identical.
- [ ] **AG3** Import again. **Expect:** "27 already in this scene"; nothing is added or rewritten.
- [ ] **AG4** Import, Undo, Redo. **Expect:** 27 → 0 → 27, each autosaved.
- [ ] **AG5** Close and reopen. **Expect:** the import is kept.
- [ ] **AG6** A QuPath-authored file with a 5-point Points class. **Expect:** shapes in place, and
  5 markers counted under the class.
- [ ] **AG7** Detections. **Expect:** counted as skipped.
- [ ] **AG8** Scene 1's export into scene 0, then Cancel. **Expect:** both warnings; nothing changes.
- [ ] **AG9** Cancel at the summary. **Expect:** nothing changes or is marked dirty.
- [ ] **AG10** A non-JSON file. **Expect:** "the file is not valid JSON".
- [ ] **AG11** **Expect:** Greek and Cyrillic arrive byte-identical.
- [ ] **AG12** **Expect:** "Imported 27 annotations from …".
- [ ] **IMP-1 Large file** (CLAUDE.md, TD-2). Import a 150 MB QuPath export with 60 000 cells, and press
  Cancel during "Reading…". **Expect:** a modal, cancellable dialog. Cancel changes nothing. Uncancelled,
  the read takes about 6 s and the detections are skipped.

### ASAP XML import (`2026-10-23-annotation-asap-import-design.md`, group AI)

- [ ] **AI1** **Expect:** the dialog offers GeoJSON and ASAP XML, and GeoJSON behaves as in AG2.
- [ ] **AI2** One of each type. **Expect:** counted, with Ellipse named as skipped and the spline noted.
  Classes and colours come from the groups, and there are no automatic labels.
- [ ] **AI3** *(open: needs a real CAMELYON file)* Groups `_0`, `_1`, `_2`, `Exclusion`. **Expect:** they
  become classes and tally rows, in the group colours.
- [ ] **AI4** Import twice. **Expect:** "already in this scene".
- [ ] **AI5** Undo; import again and reopen. **Expect:** one undo step, and the import persists.
- [ ] **AI6** `mtd.xml` and a truncated file. **Expect:** "not an ASAP annotation file" and "not valid
  XML: Premature end of document.".
- [ ] **AI7** Decimal-comma twin. **Expect:** identical shapes.
- [ ] **AI8 DTD refused** (spec §3). Import an ASAP file with a `<!DOCTYPE>` declaring an entity.
  **Expect:** refused with a warning; nothing is imported and nothing hangs.

### Measurements CSV (`2026-10-23-annotation-measurements-export-design.md`, group AH)

- [ ] **AH1** **Expect:** enabled only while a slide is open.
- [ ] **AH2** **Expect:** `<slide>.s<N>.measurements.csv` in the workspace.
- [ ] **AH3** **Expect:** 27 rows in order, with a BOM.
- [ ] **AH4** **Expect:** the panel and CSV agree to 3 significant figures.
- [ ] **AH5** *(open: Excel)* **Expect:** columns split, Greek and Cyrillic intact, and `=1+1` shown as
  text.
- [ ] **AH6** Uncalibrated slide. **Expect:** micron columns empty, pixel columns filled.
- [ ] **AH7** Bow-tie. **Expect:** area empty, `area_note` = `self-intersecting`, perimeter present.
- [ ] **AH8** **Expect:** the filter is ignored.
- [ ] **AH9** **Expect:** a read-only slide exports; an unloaded scene is refused.
- [ ] **AH10** Write-denied file. **Expect:** "Access is denied."; the file is untouched.
- [ ] **AH11** **Expect:** a status-bar confirmation.

## 7. Packaging (`2026-10-03-ci-distribution-design.md`)

- [ ] **PKG-1 Windows zip on a clean machine.** **Expect:** it starts. The Qt DLLs and
  `plugins/platforms/qwindows.dll` are present, and a slide opens.
- [ ] **PKG-2 NSIS installer.** **Expect:** it installs and launches from the Start menu, and uninstalls
  cleanly.
- [ ] **PKG-3 macOS dmg (unsigned), x86_64 and arm64.** **Expect:** it launches through the Gatekeeper
  workaround and opens a slide.
- [ ] **PKG-3a macOS Finder opens** (issue #13). With the viewer closed, then again with it running:
  use Open With on an `.svs`, and drop a `.ndpi` on the Dock icon. **Expect:** Open With lists SlideIO
  Viewer and the slide opens each time. A `.tif` lists the viewer under Open With but still opens in
  Preview on a double-click.
- [ ] **PKG-4 Debian `.deb` on clean Ubuntu 22.04.** **Expect:** `ldd` shows no "not found". It installs
  to `/opt/slideio-viewer` and starts from the CLI and the launcher.

---

## Not in this plan

- **Struck checks**, which assert something the code deliberately does not do: AB19 and AB23 (no UI
  route to a drawing tool on a read-only slide), AC9's "list keeps its selection", AC29's "Menu key
  opens on the selected row", AD14's read-only-folder route (the Windows dialog refuses it), AG1's
  associated-image clause, and AM8's last-layer delete (covered by a unit test).
- **Unbuilt features:** cases, bookmarks, split view, remote slides, plugins, Python scripting, the
  write-ahead log, and the help window with F1.
