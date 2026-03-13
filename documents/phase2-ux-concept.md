# Phase 2: UX Concept

**Author:** UX Specialist
**Date:** 2026-03-13
**Status:** Complete
**Based on:** Phase 1 Requirements Discovery (Pathologist)

---

## 1. Design Principles

### 1.1 Efficiency-First

The primary users (clinical pathologists) review 40-80 slides per day under time pressure. Every interaction must be optimized for speed:

- **Zero-dialog workflows.** Routine operations (annotation creation, slide switching, zoom) never produce modal dialogs.
- **One-click access.** The most frequent actions (select annotation tool, open next slide, fit-to-screen) require exactly one click or one keystroke.
- **Persistent tool state.** When a pathologist selects the freehand annotation tool, it stays selected across zoom/pan operations until explicitly changed.
- **Muscle-memory friendly.** Keyboard shortcuts follow industry conventions and remain stable across versions.

### 1.2 Minimal Cognitive Load

- **Progressive disclosure.** The default view shows only essential controls. Advanced panels (measurement properties, layer management, export options) appear on demand.
- **Contextual tools.** Annotation property panels appear adjacent to the selected annotation, not in a distant sidebar.
- **Consistent spatial layout.** Panels always appear in the same screen region. Users should never search for a control.
- **Clear visual hierarchy.** The slide image dominates the viewport (85%+ of screen area). Chrome is minimal and subdued.

### 1.3 Familiar Patterns

- **Map-like navigation.** Pan and zoom behavior mirrors Google Maps: scroll-wheel zoom centered on cursor, click-drag to pan, double-click to zoom in.
- **Standard OS conventions.** Native menu bar, standard keyboard shortcuts (Ctrl/Cmd+Z, Ctrl/Cmd+S, Ctrl/Cmd+C), native file dialogs.
- **Drawing tool behavior.** Annotation tools behave like drawing tools in PowerPoint/Keynote: click-drag to create shape, handles to resize, double-click to edit text.
- **Reference applications.** UI patterns informed by QuPath, ASAP, Aperio ImageScope, and Philips IntelliSite Pathology Suite, which pathologists already know.

### 1.4 Forgiveness and Recovery

- **Deep undo/redo.** Minimum 50 levels for all annotation operations.
- **Auto-save.** Every annotation action triggers an auto-save (debounced to 2 seconds of inactivity), plus periodic saves every 30 seconds.
- **Session recovery.** On crash, restore all open slides, viewport positions, and unsaved annotations.
- **Non-destructive display adjustments.** Brightness/contrast changes are display-only and never modify the source image.

### 1.5 Accessibility

- **Colorblind-safe defaults.** Default annotation palette uses blue, orange, yellow, purple, and cyan -- avoiding red/green-only distinctions.
- **Scalable UI.** All text, icons, and controls respect system DPI settings. Minimum touch target: 32x32 logical pixels.
- **High-contrast annotation mode.** Annotations render with contrasting outlines (white outer stroke, colored inner stroke) for visibility against any tissue stain.
- **Screen reader support.** All non-image UI elements (menus, panels, dialogs, lists) are accessible via screen readers.
- **Keyboard-navigable.** Every panel and control is reachable via Tab/Shift+Tab.

---

## 2. UI Layout

### 2.1 Overall Structure

```
+--------------------------------------------------------------+
| Menu Bar                                                      |
+--------------------------------------------------------------+
| Main Toolbar                                                  |
+----------+--------------------------------------+-------------+
|          |                                      |             |
| Left     |                                      | Right       |
| Panel    |        Main Viewport                 | Panel       |
| (Slides) |        (Slide Image)                 | (Props)     |
|          |                                      |             |
|          |  [Minimap]                            |             |
|          |                                      |             |
+----------+--------------------------------------+-------------+
| Status Bar                                                    |
+--------------------------------------------------------------+
```

### 2.2 Main Viewport

- Occupies the largest possible screen area (center region, expandable to full screen).
- Renders the current slide image with annotations overlaid.
- Contains a floating minimap/overview widget (top-right corner by default, repositionable).
- Contains a floating zoom indicator showing current magnification.
- Supports split view: 2x1, 1x2, or 2x2 grid for multi-slide comparison.
- Background color: neutral gray (#404040 in dark theme, #D0D0D0 in light theme) to avoid color perception bias.

### 2.3 Left Panel: Slide Tray / Case Panel

- **Default width:** 220px, resizable, collapsible.
- **Contents:**
  - Case header (accession number, patient ID, clinical history snippet).
  - Slide thumbnail grid or list, each showing: thumbnail image, stain label, block ID.
  - Slides grouped by tissue block, sortable by stain type or scan date.
- **Interactions:**
  - Single-click opens a slide in the main viewport.
  - Drag a slide onto a comparison panel to add it to split view.
  - Right-click for context menu: "Open in new window", "Show metadata", "Flag quality issue".
  - Hover shows an enlarged preview tooltip (300x300px) including the slide label/macro image (if available) for physical slide verification.
- **Slide label image:** Each slide entry in the case panel can display a small label/macro image thumbnail (the physical slide label captured by the scanner). Clicking the thumbnail opens a larger popup. This allows pathologists to verify they are viewing the correct slide. The label image can be hidden via preferences for privacy (it may contain patient information).
- **Collapsed state:** Shows only a vertical strip of mini-thumbnails (48px wide) for maximum viewport space.

### 2.4 Right Panel: Properties and Annotations

- **Default width:** 260px, resizable, collapsible.
- **Tabbed sub-panels:**
  1. **Annotations Tab:** List of all annotations on the current slide, grouped by layer. Each entry shows: icon (shape type), label text, classification badge, author. Clicking an annotation in the list selects it on the slide and zooms to fit it.
  2. **Properties Tab:** Shows properties of the currently selected annotation (color, label, classification, measurements, author, timestamp). Editable inline.
  3. **Metadata Tab:** Scanner metadata, slide-level metadata, case-level metadata. Read-only display with copy-to-clipboard buttons.
  4. **Layers Tab:** Annotation layer management. Toggle visibility, lock/unlock, set opacity, reorder. Create/rename/delete layers.
  5. **Channels Tab** (appears only for multi-channel fluorescence slides): Per-channel visibility toggles, pseudo-color assignment (color picker per channel), per-channel brightness/contrast sliders, and a composite view toggle. Channels are listed by name (e.g., DAPI, FITC, Cy3) with their assigned display color. This panel supports the growing use of multiplex immunofluorescence in research and companion diagnostics.

### 2.5 Main Toolbar

Horizontal toolbar below the menu bar. Organized into logical groups separated by dividers:

```
[Navigation Group] | [Annotation Group] | [View Group] | [Tools Group]

Navigation:    [Pan] [Zoom In] [Zoom Out] [Fit] [1:1]
Annotation:    [Select] [Rect] [Ellipse] [Freehand] [Point] [Arrow] [Ruler] [Area] [Text] [Angle]
View:          [Split 1x1] [Split 2x1] [Split 1x2] [Split 2x2] [Sync Lock]
Tools:         [Brightness/Contrast] [Snapshot] [Scale Bar Toggle]
```

- **Button style:** 24x24px icons with optional text labels (user-configurable: icons only, icons+text, text only).
- **Active tool highlight:** The currently active tool has a depressed/highlighted state.
- **Annotation tool dropdown:** Each shape tool has a small dropdown arrow for variant selection (e.g., the Freehand tool dropdown offers "Polygon" and "Polyline").
- **Toolbar customization:** Users can show/hide toolbar groups and rearrange them.

### 2.6 Status Bar

Bottom bar showing:
- **Left:** Current magnification (e.g., "20.0x") and pixel coordinates under cursor (e.g., "X: 45230, Y: 12890").
- **Center:** Scale bar (physical units, e.g., "100 um").
- **Right:** Tile loading progress indicator (subtle spinner or progress bar during tile loading), memory usage indicator, connection status for remote slides.

### 2.7 Menu Structure

```
File
  Open Slide...           Ctrl+O
  Open Case...            Ctrl+Shift+O
  Recent Files            >
  Close Slide             Ctrl+W
  Close All               Ctrl+Shift+W
  ---
  Import Annotations...
  Export Annotations...
  Export Snapshot...       Ctrl+Shift+S
  ---
  Exit                    Alt+F4

Edit
  Undo                    Ctrl+Z
  Redo                    Ctrl+Y / Ctrl+Shift+Z
  ---
  Cut                     Ctrl+X
  Copy                    Ctrl+C
  Paste                   Ctrl+V
  Delete                  Delete
  Select All Annotations  Ctrl+A
  ---
  Preferences...          Ctrl+,

View
  Zoom In                 Ctrl++
  Zoom Out                Ctrl+-
  Fit to Screen           Ctrl+0
  Actual Pixels (1:1)     Ctrl+1
  ---
  Magnification           > 2x, 4x, 10x, 20x, 40x
  ---
  Full Screen             F11
  ---
  Left Panel              Ctrl+L
  Right Panel             Ctrl+R
  Status Bar              (toggle)
  Toolbar                 (toggle)
  Minimap                 (toggle)
  Scale Bar               (toggle)
  ---
  Theme                   > Light, Dark, System

Slide
  Next Slide              Page Down
  Previous Slide          Page Up
  ---
  Split View              > 1x1, 2x1, 1x2, 2x2
  Synchronized Navigation (toggle)
  ---
  Brightness/Contrast...  Ctrl+B
  Reset Display           Ctrl+Shift+R
  ---
  Slide Information...
  ---
  Rotate Left             Ctrl+Shift+Left
  Rotate Right            Ctrl+Shift+Right
  Reset Rotation

Annotations
  Select Tool             V
  Rectangle               R
  Ellipse                 E
  Freehand Polygon        F
  Point Marker            P
  Arrow                   A
  Ruler                   M (measure)
  Area Measurement        Shift+M
  Text Label              T
  Angle Measurement       G
  ---
  Lock Layer
  Show/Hide Layer
  ---
  Annotation Properties   Ctrl+I

Bookmarks
  Add Bookmark...         Ctrl+Shift+B
  Manage Bookmarks...
  ---
  (saved bookmarks appear here)

Tools
  Snapshot                Ctrl+Shift+S
  Annotation Summary
  ---
  Scale Bar Settings...
  Measurement Units       > Micrometers, Millimeters

Help
  User Guide              F1
  Keyboard Shortcuts      Ctrl+/
  ---
  About SlideIO Viewer
```

---

## 3. Navigation in Large Images

### 3.1 Zoom Interaction Model

| Input | Action |
|-------|--------|
| Mouse scroll wheel up | Zoom in, centered on cursor position |
| Mouse scroll wheel down | Zoom out, centered on cursor position |
| Ctrl+= / Ctrl+Scroll | Fine-grain zoom (smaller increments) |
| Double-click (left) | Zoom in one step at click position |
| Pinch gesture (trackpad) | Continuous zoom centered on pinch center |
| Keyboard +/- | Zoom in/out centered on viewport center |
| Number keys 1-5 | Jump to 2x, 4x, 10x, 20x, 40x (matching microscope objectives) |

**Zoom behavior:**
- Continuous (smooth) zoom, not discrete steps. The zoom factor follows a logarithmic scale so equal scroll increments produce equal perceptual magnification changes.
- Zoom range: approximately 0.25x (entire slide with margins) to the slide's native resolution (40x or 60x).
- Zoom animation: 150ms ease-out transition for discrete zoom jumps (number keys, double-click). Scroll wheel zoom is instantaneous (no animation delay).
- Fit-to-screen (Ctrl+0): Animated transition (300ms) to show the entire tissue region with 5% margin.

### 3.2 Pan Interaction Model

| Input | Action |
|-------|--------|
| Left mouse drag (when Pan tool active or Space held) | Pan the viewport |
| Middle mouse drag (always) | Pan the viewport |
| Arrow keys | Pan by 10% of viewport in the arrow direction |
| Shift+Arrow keys | Pan by 50% of viewport |
| Touch drag (trackpad, two fingers) | Pan the viewport |

**Pan behavior:**
- Pan is always available via middle mouse button, regardless of the active tool. This allows panning while in annotation mode without switching tools.
- Holding Space temporarily activates pan mode (releases back to previous tool on Space release). This mirrors Photoshop convention.
- Optional inertial panning (configurable): releasing a fast drag continues movement with friction-based deceleration. Clicking stops inertial movement immediately.
- Viewport is clamped to slide bounds with 10% overscroll padding.

### 3.3 Minimap / Overview Window

- **Position:** Floating widget, default top-right corner of the viewport. Draggable to any corner.
- **Size:** 180x180px default, resizable by dragging edges (range: 100px - 300px).
- **Content:** Shows the entire slide thumbnail with:
  - A blue rectangle indicating the current viewport extent.
  - Colored dots/regions showing annotation locations (optional, toggleable).
  - Tissue auto-detection highlights (dimming the non-tissue background).
- **Interaction:**
  - Click on the minimap: jumps the main viewport to center on the clicked position.
  - Drag the blue rectangle on the minimap: pans the main viewport correspondingly.
  - Scroll wheel on the minimap: zooms the main viewport.
- **Visibility:** Toggleable via View menu or keyboard shortcut (Ctrl+M). Auto-hides during full-screen presentation mode unless explicitly pinned.

### 3.4 Z-Stack Focus Control

For slides with multiple focal planes (common in cytology and thick tissue sections):

- **Z-level slider:** A small vertical slider on the left edge of the viewport, visible only when the current slide has multiple Z-planes.
- **Interaction:** Drag the slider to change the focal plane. The slider shows the current Z-level index (e.g., "Z: 5/11").
- **Keyboard:** Ctrl+Up / Ctrl+Down to step through Z-levels one plane at a time.
- **Mouse:** Ctrl+Scroll wheel adjusts Z-level (when the Z-slider is visible), distinguishing from regular scroll-wheel zoom.
- **Auto-hide:** The Z-slider is hidden for single-focal-plane slides to avoid UI clutter.
- **Extended focus:** An optional "Best Focus" button composites the sharpest regions from all Z-planes into a single view (computationally intensive, may require background processing).

### 3.5 View Rotation

Some tissue sections are scanned at an angle. Pathologists occasionally need to rotate the view:

- **Quick rotation:** View > Rotate menu with 90-degree increment options (Rotate Left, Rotate Right, Rotate 180, Reset Rotation).
- **Keyboard:** Ctrl+Shift+Left / Ctrl+Shift+Right for 90-degree rotation increments.
- **Free rotation:** A rotation widget (circular handle) accessible from the toolbar or via Ctrl+Shift+Drag. Allows arbitrary angle rotation.
- **Snap angles:** During free rotation, the view snaps to 0, 90, 180, 270 degrees when within 5 degrees of those values.
- **Rotation indicator:** When the view is rotated, a small compass indicator appears in the viewport corner showing the rotation angle.
- **Coordinate system:** Rotation is display-only. Annotations and measurements remain in the slide's native coordinate system, unaffected by view rotation.

### 3.6 Zoom Level Indicator

- Floating widget in the bottom-left of the viewport (above the status bar).
- Displays current magnification as "20.0x" and a horizontal zoom slider.
- Clicking the magnification text opens a dropdown for direct entry or preset selection.
- The zoom slider allows click-drag for continuous zoom, or click on the track for jump-to zoom.

### 3.7 Navigation History

- **Back/Forward:** Ctrl+Left / Ctrl+Right (or browser-style mouse buttons 4/5) navigate through a history of viewport states.
- A viewport state is recorded when: the user stops panning/zooming for 1 second, switches slides, or jumps to a bookmark.
- History depth: 100 entries per session.

### 3.8 Bookmarks

- **Creating:** Ctrl+Shift+B saves the current state (slide, position, zoom, active layers) as a named bookmark.
- **Quick-save:** Shift+1 through Shift+9 save to numbered quick-bookmark slots (no dialog).
- **Quick-recall:** Alt+1 through Alt+9 jump to the corresponding quick-bookmark.
- **Bookmark panel:** Bookmarks listed in the Bookmarks menu and an optional bookmarks sidebar.
- **Bookmarks persist** with the case file.

---

## 4. Annotation Tools

### 4.1 Tool Selection and Modes

The application operates in two primary modes:

1. **Navigation Mode (default):** Left-click-drag pans. The cursor is an open hand (grabbing hand while dragging). This is the "Pan" tool.
2. **Annotation Mode:** An annotation tool is active. Left-click-drag creates or modifies annotations. Pan is available via middle mouse button or Space+drag.

Switching between modes:
- Press V to return to the Select/Navigate tool.
- Press a tool hotkey (R, E, F, P, A, M, T, G) to enter annotation mode with that tool.
- Press Escape to deselect the current annotation and return to navigation mode.

### 4.2 Drawing Tools Behavior

#### Rectangle (R)
- Click-drag to define opposite corners.
- Hold Shift while dragging to constrain to a square.
- Visual feedback: dashed outline during creation, solid on release.

#### Ellipse (E)
- Click-drag to define the bounding box of the ellipse.
- Hold Shift to constrain to a circle.
- Hold Alt to draw from center outward.

#### Freehand Polygon (F)
- Click to place vertices; double-click or click on the start vertex to close the polygon.
- Alternatively: hold-and-drag for freehand drawing (vertices auto-generated at intervals).
- Press Backspace to remove the last placed vertex during creation.
- After creation: double-click the polygon to enter vertex editing mode.

#### Point Marker (P)
- Single-click to place a marker pin.
- Marker icon: filled circle with a customizable color and optional label.
- Markers are always visible regardless of zoom level (screen-space size).

#### Arrow (A)
- Click-drag from tail to head.
- Arrowhead style: filled triangle (configurable size).

#### Ruler / Distance Measurement (M)
- Click-drag to define a line segment.
- Length is displayed alongside the ruler line in physical units (um or mm) using the slide's calibration metadata.
- The measurement label repositions automatically to avoid overlapping the ruler line.
- After completing a measurement, the annotation remains selected and its measurement label is prominently displayed, even as the tool stays active for the next measurement. This allows the pathologist to read the result before proceeding.

#### Area Measurement (Shift+M)
- Same interaction as Freehand Polygon, but the area is calculated and displayed inside the polygon in physical units (um^2 or mm^2).
- Perimeter is also calculated and available in properties.

#### Text Label (T)
- Click to place; an inline text editor appears immediately (no dialog).
- Press Enter to confirm, Escape to cancel.
- Font size is defined in screen-space points but the anchor position is in slide coordinates.

#### Angle Measurement (G)
- Click to place the vertex, drag to define the first ray, click again to define the second ray.
- Angle value displayed at the vertex in degrees.

### 4.3 Annotation Selection and Editing

- **Select tool (V):** Click an annotation to select it. Shift+click to add to selection. Click on empty area to deselect all.
- **Rubber-band selection:** Click-drag on empty area draws a selection rectangle; all annotations fully inside are selected.
- **Selected annotation indicators:** Corner/edge handles for resize, a move cursor when hovering the interior.
- **Multi-select operations:** Move, delete, change color/classification, copy/paste apply to all selected annotations.
- **Right-click context menu** on an annotation:
  ```
  Edit Properties...
  Edit Vertices        (polygons only)
  ---
  Bring to Front
  Send to Back
  ---
  Move to Layer        > (layer list)
  ---
  Copy                 Ctrl+C
  Duplicate            Ctrl+D
  Delete               Delete
  ```

### 4.4 Annotation Property Panel

When an annotation is selected, the right panel's Properties tab shows:

- **Type:** (read-only) Rectangle, Ellipse, Polygon, etc.
- **Label:** Editable text field.
- **Classification:** Dropdown with configurable taxonomy (Tumor, Stroma, Necrosis, Normal, Artifact, custom). Color swatch auto-assigned by classification.
- **Color:** Color picker. Default linked to classification, but overridable.
- **Line width:** Slider (1-5 screen pixels).
- **Fill opacity:** Slider (0-100%). 0% = outline only, 30% = default for region highlights.
- **Measurements:** (read-only, for measurement annotations) Length, area, angle.
- **Author:** (read-only) Auto-populated from user identity.
- **Created / Modified:** (read-only) Timestamps.
- **Confidence:** Optional dropdown (Certain, Probable, Possible) for research use.
- **Notes:** Multi-line text field for extended comments.

### 4.5 Annotation Layers

- **Default layers:** "Diagnostic" (default for new annotations), "Teaching", "Research".
- **Layer panel:** Shows layer list with visibility toggle (eye icon), lock toggle (padlock icon), and opacity slider.
- **Layer operations:** Create, rename, delete, merge, reorder (drag-and-drop).
- **Filter by author:** A separate filter in the annotation list allows showing/hiding annotations by author, independent of layers.
- **Active layer:** New annotations are created on the active (highlighted) layer. Clearly indicated in the status bar.

### 4.6 Quick Classification

For high-throughput annotation (e.g., marking 50 mitotic figures or tumor foci), navigating to the properties panel for each annotation is too slow. Two mechanisms address this:

1. **Tool-level default classification:** Right-click any annotation tool button in the toolbar to open a "Tool Defaults" popup. Set a default classification (e.g., "Tumor") and color. All subsequent annotations created with that tool inherit these defaults automatically. The toolbar button shows a small colored dot indicating the active default classification.

2. **Floating quick-classification bar:** After completing an annotation, a small floating bar appears adjacent to the new annotation for 3 seconds (or until dismissed). It shows the most recently used classifications as colored buttons (e.g., [Tumor] [Normal] [Necrosis]). Single-click to assign. The bar auto-hides if the user starts drawing the next annotation, keeping the workflow uninterrupted. This bar can be disabled in preferences for users who prefer the properties panel.

### 4.7 Snap and Alignment Features

- **Snap-to-grid:** Optional grid overlay (configurable spacing in microns), annotations snap to grid intersections when enabled.
- **Snap-to-annotation:** When dragging near an existing annotation edge or vertex, snap guides appear and the annotation aligns.
- **Smart guides:** Horizontal/vertical alignment guides appear when moving annotations into alignment with other annotations.
- **All snap features togglable** via a toolbar toggle button or keyboard shortcut (Ctrl+Shift+G for grid, Ctrl+Shift+S for snap-to-annotation).

---

## 5. Workflow Descriptions

### 5.1 Slide Review Workflow (Primary Diagnosis)

```
User Action                          UI Response
-----------                          -----------
1. Opens case from File menu         Left panel populates with slide list
   or drag-and-drop                  and case metadata. First slide loads
                                     in viewport at fit-to-screen zoom.

2. Clicks a slide in the left panel  Slide loads with 200ms transition.
                                     Previous slide's viewport state is saved.

3. Scrolls wheel to zoom to ~4x     Continuous zoom centered on cursor.
                                     Minimap updates to show viewport extent.

4. Drags to pan across tissue        Smooth panning. Status bar shows
                                     coordinates and magnification.

5. Spots area of interest,           Smooth zoom animation. Tile loading
   scrolls to 20x                    shows progressive refinement (blurry
                                     then sharp within 200-300ms).

6. Presses R, draws rectangle        Rectangle annotation created on
   around region                     active layer. Properties panel shows
                                     annotation details.

7. Types label in properties         Label text appears on the annotation.
   panel: "Invasive carcinoma"

8. Presses Page Down                 Next slide in case loads. Previous
                                     viewport state and annotations saved.

9. Reviews remaining slides          Same flow repeats.

10. Presses Ctrl+Shift+S             Snapshot dialog captures current
    for snapshot                      viewport as PNG with annotations
                                     and scale bar.
```

### 5.2 Annotation Creation Workflow

```
1. Select tool (press F for freehand polygon)
   -> Cursor changes to crosshair
   -> Toolbar highlights the Freehand tool
   -> Status bar shows: "Freehand Polygon | Click to place vertices, double-click to close"

2. Click to place first vertex
   -> Small vertex marker appears on slide

3. Click to place additional vertices
   -> Edges drawn between vertices with dashed lines
   -> Press Backspace to undo last vertex

4. Double-click to close the polygon
   -> Polygon closes, fill renders at default opacity
   -> Annotation auto-selected, properties panel activates
   -> Auto-save triggers

5. Set classification from dropdown in properties panel
   -> Color updates to classification color
   -> Classification badge appears on annotation list

6. Optionally press F again to draw another polygon
   -> Tool remains active for rapid sequential annotation
```

### 5.3 Multi-Slide Comparison Workflow

```
1. Select View > Split View > 2x1 (or press Ctrl+2)
   -> Viewport splits into left and right panels
   -> Current slide remains in the left panel
   -> Right panel shows "Drop a slide here" placeholder

2. Drag a slide from the left case panel to the right viewport
   -> Right panel loads the dragged slide at fit-to-screen

3. Click the "Sync Lock" button in the toolbar (or press Ctrl+Shift+L)
   -> Lock icon activates
   -> Both panels now zoom and pan in synchronization

4. Scroll to zoom on either panel
   -> Both panels zoom together
   -> Corresponding regions shown side by side

5. Click Sync Lock again to unlock for independent navigation
   -> Panels navigate independently

6. Press Ctrl+1 to return to single-panel view
   -> Right panel closes, left panel expands to full viewport
```

### 5.4 Case Review / Tumor Board Workflow

```
1. Open case, then press F11 for full-screen mode
   -> Menu bar, toolbar, and side panels hide
   -> Viewport fills the entire screen
   -> Floating mini-toolbar appears at bottom with essential controls
   -> Minimap remains visible (if pinned)

2. Navigate with mouse to demonstrate findings
   -> Clean, distraction-free presentation

3. Press Escape or F11 to exit full-screen
   -> All panels restore to previous layout

4. For multi-monitor: undock the case panel to secondary monitor
   -> Drag panel's title bar off the main window
   -> Panel becomes a floating window on the second monitor
   -> Slide viewport now has the full primary monitor
```

### 5.5 Teaching Workflow

```
1. Instructor opens a teaching case with pre-annotated slides
2. Sets the "Teaching" layer to visible, "Diagnostic" layer to hidden
   -> Students see instructor's guiding annotations but not answers

3. Students create their own annotations on a "Student" layer
4. After exercise, instructor reveals the "Diagnostic" layer
   -> Students compare their annotations with the reference

5. Instructor captures snapshots at key teaching points
   -> Snapshots with visible annotations exported for lecture slides
```

---

## 6. Keyboard Shortcuts

### 6.1 Navigation

| Shortcut | Action |
|----------|--------|
| Scroll Wheel | Zoom in/out at cursor |
| Middle Mouse Drag | Pan |
| Space + Left Drag | Pan (temporary) |
| Ctrl+0 | Fit to screen |
| Ctrl+1 | Actual pixels (1:1) |
| 1-5 | Jump to 2x / 4x / 10x / 20x / 40x (microscope objectives) |
| +/- | Zoom in/out (viewport center) |
| Arrow Keys | Pan 10% of viewport |
| Shift+Arrow Keys | Pan 50% of viewport |
| Ctrl+Left / Ctrl+Right | Navigation history back/forward |
| Page Up / Page Down | Previous / next slide in case |
| Home | First slide in case |
| End | Last slide in case |
| F11 | Toggle full screen |
| Ctrl+Up / Ctrl+Down | Z-stack: previous / next focal plane |
| Ctrl+Shift+Left / Ctrl+Shift+Right | Rotate view 90 degrees |

### 6.2 Annotation Tools

| Shortcut | Tool |
|----------|------|
| V | Select / Navigate |
| R | Rectangle |
| E | Ellipse |
| F | Freehand Polygon |
| P | Point Marker |
| A | Arrow |
| M | Ruler (distance measurement) |
| Shift+M | Area measurement |
| T | Text label |
| G | Angle measurement |
| Escape | Deselect / cancel current operation |

### 6.3 Editing

| Shortcut | Action |
|----------|--------|
| Ctrl+Z / Cmd+Z | Undo |
| Ctrl+Y / Cmd+Shift+Z | Redo |
| Ctrl+C | Copy annotation(s) |
| Ctrl+V | Paste annotation(s) |
| Ctrl+D | Duplicate annotation(s) |
| Delete / Backspace | Delete selected annotation(s) |
| Ctrl+A | Select all annotations |
| Ctrl+I | Show annotation properties |

### 6.4 View and Panels

| Shortcut | Action |
|----------|--------|
| Ctrl+L | Toggle left panel (slide tray) |
| Ctrl+R | Toggle right panel (properties) |
| Ctrl+M | Toggle minimap |
| Ctrl+2 | Split view 2x1 |
| Ctrl+3 | Split view 1x2 |
| Ctrl+4 | Split view 2x2 |
| Ctrl+Shift+L | Toggle synchronized navigation |
| Tab | Cycle focus between viewport and panels |

### 6.5 File and Bookmarks

| Shortcut | Action |
|----------|--------|
| Ctrl+O | Open slide |
| Ctrl+Shift+O | Open case |
| Ctrl+W | Close current slide |
| Ctrl+Shift+S | Capture snapshot |
| Ctrl+Shift+B | Add bookmark |
| Shift+1 through Shift+9 | Save quick bookmark |
| Alt+1 through Alt+9 | Recall quick bookmark |
| Ctrl+/ | Show keyboard shortcut reference |
| Ctrl+, | Open preferences |

### 6.6 Modifier Key Behavior During Annotation

| Modifier | Effect |
|----------|--------|
| Shift | Constrain proportions (square rectangle, circle ellipse) |
| Alt | Draw from center outward |
| Ctrl+Drag | Duplicate and move (on selected annotation) |
| Shift+Click | Add to / remove from selection |

---

## 7. Interaction Patterns

### 7.1 Context Menus

Right-click context menus are context-sensitive:

**On empty slide area:**
```
Fit to Screen
Zoom to...              > 1x, 5x, 10x, 20x, 40x
---
Paste Annotation        Ctrl+V
---
Add Bookmark Here       Ctrl+Shift+B
---
Slide Information...
```

**On an annotation:**
```
Edit Properties...      Ctrl+I
Edit Vertices           (polygons only)
---
Bring to Front
Send to Back
---
Move to Layer           > (layer list)
---
Cut                     Ctrl+X
Copy                    Ctrl+C
Duplicate               Ctrl+D
Delete                  Delete
```

**On the slide tray (left panel):**
```
Open Slide
Open in New Window
Open in Split Panel     > Left, Right, Top-Left, Top-Right...
---
Show Metadata
Flag Quality Issue      > Focus, Staining, Artifact, Other
```

### 7.2 Drag-and-Drop

| Source | Target | Action |
|--------|--------|--------|
| Slide file (OS file explorer) | Application window | Open slide / create case |
| Slide thumbnail (left panel) | Viewport | Open slide in viewport |
| Slide thumbnail (left panel) | Split panel | Open slide in that panel |
| Annotation (viewport) | Different layer (layer panel) | Move annotation to layer |
| Annotation file (.json) | Application window | Import annotations |

### 7.3 Multi-Select Patterns

- **Shift+Click:** Toggle individual annotation in selection.
- **Ctrl+A:** Select all annotations on visible layers.
- **Rubber-band:** Click-drag on empty area creates selection rectangle.
- **Annotation list:** Click, Shift+Click (range), Ctrl+Click (toggle) in the annotation list panel.
- **Batch operations on multi-select:** Delete, move to layer, change classification, change color, copy/cut.

### 7.4 Undo/Redo

- **Scope:** Undo/redo operates per-slide. Switching slides switches the undo stack.
- **Tracked operations:** Create, delete, move, resize, reshape, property change, paste, layer change.
- **Not tracked:** Navigation (pan/zoom), panel visibility, display adjustments (brightness/contrast).
- **Stack depth:** 50 levels minimum, configurable up to 200.
- **Visual feedback:** Brief tooltip near the toolbar showing the undone/redone action (e.g., "Undo: Delete Rectangle").

### 7.5 Tooltips and Hints

- **Toolbar buttons:** Tooltip on hover showing tool name + keyboard shortcut (e.g., "Freehand Polygon (F)").
- **Status bar hints:** When a tool is active, the status bar shows usage instructions (e.g., "Click to place vertices, double-click to close polygon").
- **First-run hints:** On first launch, subtle coach marks highlight key UI areas (dismissible, not a blocking tour).

---

## 8. Multi-Monitor Support

### 8.1 Detachable Panels

All side panels can be detached from the main window and positioned independently:

- **Undock:** Drag the panel's title bar away from the main window, or double-click the title bar.
- **Redock:** Drag the floating panel's title bar back to a dock zone (left/right edges of the main window show drop highlights).
- **Independent windows:** Floating panels are proper OS windows that can be minimized, resized, and moved to any monitor.

Detachable panels:
- Left panel (slide tray / case panel)
- Right panel (annotations / properties / metadata / layers)
- Bookmark panel
- Minimap (as a standalone floating window)

### 8.2 Multi-Window Viewing

- **New window:** File > Open in New Window, or right-click a slide > "Open in New Window".
- **Each window** is an independent viewer instance sharing the same case data and annotation store.
- **Annotation synchronization:** Changes to annotations in one window are reflected in all windows viewing the same slide (within the same process).
- **Use case:** One monitor shows the full slide viewport, the other shows properties panels and the annotation list, or two monitors show two different slides from the same case.

### 8.3 Saved Layouts

- **Save layout:** Window > Save Layout As... saves positions, sizes, panel docking state, and split view configuration.
- **Preset layouts:**
  - "Single Monitor" -- all panels docked, no split view.
  - "Dual Monitor - Clinical" -- full viewport on monitor 1, all panels on monitor 2.
  - "Dual Monitor - Comparison" -- split view on monitor 1, case panel on monitor 2.
  - "Presentation" -- full screen viewport, no panels.
- **Restore layout:** Window > Layouts > (saved layout name).
- **Auto-restore:** The last used layout is restored on application launch.

---

## 9. Performance Perception

### 9.1 Progressive Tile Loading

When zooming into a new region, the viewer must maintain visual continuity:

1. **Immediate:** Display the current (lower-resolution) tile data scaled up (blurry but immediate).
2. **Fast (~100-200ms):** Replace with the correct resolution tile as it decodes.
3. **Progressive refinement:** If the target tile is still loading, interpolate from the nearest available resolution level.

**Visual appearance:** The transition from blurry to sharp should be a smooth crossfade (100ms), not a jarring pop. The user should perceive continuous image availability, never a blank or gray tile.

### 9.2 Prefetching Strategy (UX perspective)

- **Spatial prefetch:** Tiles adjacent to the current viewport (1 tile ring) are prefetched in the background.
- **Zoom prefetch:** One zoom level above and below the current level are prefetched for the current viewport area.
- **Predictive prefetch:** If the user is panning in a direction, prioritize prefetching tiles ahead of the pan direction.
- **UX requirement:** Prefetching must not cause UI jank. Tile decoding happens on background threads with lower priority than the current viewport tiles.

### 9.3 Loading Indicators

- **Tile-level:** No per-tile loading indicator (too visually noisy). Instead, use the blurry-to-sharp progressive refinement described above.
- **Slide-level:** When opening a slide, show a centered spinner over the viewport with the text "Loading [slide name]..." for the initial 1-2 seconds.
- **Status bar:** A subtle progress indicator (thin progress bar or spinner icon) in the status bar shows when background tile loading is active. It disappears when all visible tiles are loaded.
- **Remote loading:** For remote slides with higher latency, show a semi-transparent overlay with a progress bar and estimated time remaining if loading exceeds 3 seconds.

### 9.4 Smooth Transitions

- **Slide switching:** Crossfade transition (200ms) between the previous and new slide.
- **Fit-to-screen / zoom-to-bookmark:** Animated zoom+pan transition (300ms ease-out).
- **Panel show/hide:** Slide animation (150ms) for panel collapse/expand.
- **Split view resize:** Live resize with smooth viewport adjustment.
- **All animations are cancellable:** Starting a new navigation action immediately cancels any in-flight animation.

### 9.5 Responsiveness Guarantees

- **Input latency:** Mouse/keyboard input must be processed within 16ms (one frame at 60fps). Tile decoding never blocks the UI thread.
- **Pan/zoom smoothness:** Target 60fps for pan and zoom operations. If the GPU cannot maintain 60fps, scale quality (reduce tile resolution temporarily) rather than dropping frames.
- **Annotation drawing:** Freehand drawing input is sampled at the full input rate and rendered immediately. Line smoothing (Catmull-Rom or similar) is applied after mouse release.

---

## 10. Theming and Visual Design

### 10.1 Color Themes

- **Light theme:** White/light gray chrome, dark text. Best for well-lit clinical environments.
- **Dark theme:** Dark gray chrome (#2D2D2D background, #E0E0E0 text). Best for tumor board rooms and dimmed environments. Reduces eye strain during long sessions.
- **System theme:** Follow the OS light/dark mode setting.
- **Viewport background** is always a neutral gray regardless of theme to avoid color bias.

### 10.2 Annotation Default Colors by Classification

| Classification | Color (Hex) | Rationale |
|---------------|-------------|-----------|
| Tumor | #E67E22 (Orange) | High visibility, colorblind-safe |
| Stroma | #3498DB (Blue) | Distinct from warm tissue colors |
| Necrosis | #9B59B6 (Purple) | Distinct from tissue, annotation |
| Normal | #2ECC71 (Green) | "Safe" / normal connotation |
| Artifact | #E74C3C (Red) | Warning / problem indicator |
| Unclassified | #95A5A6 (Gray) | Neutral default |

All colors are user-configurable in preferences.

### 10.3 Icon Design

- **Style:** Simple, outlined (not filled), monochrome icons that follow the theme.
- **Size:** 24x24px at 1x DPI, provided as SVGs for crisp rendering at all scales.
- **Active state:** Filled variant or accent-color background.

---

## 11. Error States and Edge Cases

### 11.1 Slide Load Failure

- Display a placeholder in the viewport: centered error icon + message "Unable to open [filename]: [error description]".
- The slide entry in the case panel shows a warning badge.
- Other slides in the case remain accessible.

### 11.2 Corrupt Tiles

- If individual tiles fail to decode, display a subtle hatched pattern in that tile area with a small warning icon.
- Surrounding tiles render normally. The status bar shows "N tiles failed to load" with a clickable link to details.

### 11.3 Network Interruption (Remote Slides)

- Display cached tiles at whatever resolution is available.
- A banner at the top of the viewport: "Connection lost. Showing cached data. Reconnecting..."
- Auto-retry connection every 5 seconds. On reconnection, resume tile loading seamlessly.
- Annotations continue to work locally. They sync when the connection is restored.

### 11.4 Insufficient Memory

- As memory usage approaches the configured limit, display a status bar warning: "High memory usage (3.5/4.0 GB)".
- The tile cache evicts aggressively. The user may notice more tile reloading (blurry-to-sharp transitions) but the application does not crash.

---

*This document serves as input for Phase 3 (System Architecture) and ultimately feeds into Document 2 (User Interface Design).*
