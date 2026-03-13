# Document 2 -- User Interface Design

**Project:** SlideIO Viewer -- Cross-Platform Pathology Slide Viewer
**Version:** 1.0
**Date:** 2026-03-13

---

## Table of Contents

1. [Design Principles](#1-design-principles)
2. [UI Layout](#2-ui-layout)
3. [Navigation in Large Images](#3-navigation-in-large-images)
4. [Annotation Tools](#4-annotation-tools)
5. [Workflow Descriptions](#5-workflow-descriptions)
6. [Keyboard Shortcuts](#6-keyboard-shortcuts)
7. [Interaction Patterns](#7-interaction-patterns)
8. [Multi-Monitor Support](#8-multi-monitor-support)
9. [Performance Perception](#9-performance-perception)
10. [Theming and Visual Design](#10-theming-and-visual-design)
11. [Error States and Edge Cases](#11-error-states-and-edge-cases)
12. [Privacy and Presentation Mode](#12-privacy-and-presentation-mode)

---

## 1. Design Principles

### 1.1 Efficiency-First

The primary users -- clinical pathologists -- review 40-80 slides per day under sustained time pressure. Every interaction must minimize friction:

- **Zero-dialog workflows.** Routine operations (annotation creation, slide switching, zoom) never produce modal dialogs. No confirmations for non-destructive actions.
- **One-click access.** The most frequent actions (select annotation tool, open next slide, fit-to-screen) require exactly one click or one keystroke.
- **Persistent tool state.** When a pathologist selects an annotation tool, it stays selected across zoom/pan operations until explicitly changed. A configurable preference ("Return to navigation mode after annotation") allows novice users to auto-revert to navigation mode after each annotation, but this defaults to OFF for power users.
- **Muscle-memory friendly.** Keyboard shortcuts follow industry conventions (Ctrl/Cmd+Z for undo, Ctrl/Cmd+S for save) and remain stable across application versions. All shortcuts are user-configurable in preferences.

### 1.2 Minimal Cognitive Load

- **Progressive disclosure.** The default view shows only essential controls. Advanced panels (measurement properties, layer management, export options, fluorescence channels) appear on demand or when contextually relevant.
- **Contextual tools.** Annotation property editing appears adjacent to the selected annotation via floating panels, not exclusively in a distant sidebar.
- **Consistent spatial layout.** Panels always appear in the same screen region. Users never need to search for a control.
- **Clear visual hierarchy.** The slide image dominates the viewport (85%+ of screen area). Chrome is minimal and subdued.

### 1.3 Familiar Patterns

- **Map-like navigation.** Pan and zoom behavior mirrors Google Maps: scroll-wheel zoom centered on cursor, click-drag to pan, double-click to zoom in. This is the dominant mental model for pathologists who have used any digital slide viewer.
- **Standard OS conventions.** Native menu bar, standard keyboard shortcuts, native file dialogs, drag-and-drop from the OS file explorer.
- **Drawing tool behavior.** Annotation tools behave like drawing tools in PowerPoint/Keynote: click-drag to create shape, handles to resize, double-click to edit text.
- **Reference applications.** UI patterns informed by QuPath, ASAP, Aperio ImageScope, and Philips IntelliSite Pathology Suite, which pathologists already know.

### 1.4 Forgiveness and Recovery

- **Deep undo/redo.** Minimum 50 levels for all annotation operations, configurable up to 200.
- **Auto-save.** Every annotation action triggers an auto-save (debounced to 2 seconds of inactivity), plus periodic saves every 30 seconds.
- **Session recovery.** On unexpected termination, restore all open slides, viewport positions, and unsaved annotations from the recovery journal.
- **Non-destructive display adjustments.** Brightness, contrast, and rotation changes are display-only and never modify the source image.

### 1.5 Accessibility

- **Colorblind-safe defaults.** Default annotation palette uses blue (#3498DB), orange (#E67E22), yellow, purple (#9B59B6), and cyan -- avoiding red/green-only distinctions.
- **Scalable UI.** All text, icons, and controls respect system DPI settings. Minimum touch target: 32x32 logical pixels.
- **High-contrast annotation mode.** Annotations render with dual-stroke outlines (white outer stroke, colored inner stroke) for visibility against any tissue stain color.
- **Screen reader support.** All non-image UI elements (menus, panels, dialogs, lists) are accessible via platform accessibility APIs.
- **Viewport accessibility summary.** An optional accessibility mode provides a text description of the current viewport state (magnification level, visible annotation count and labels, spatial context) exposed to screen readers without visual rendering.
- **Keyboard-navigable.** Every panel and control is reachable via Tab/Shift+Tab. Focus indicators are clearly visible.
- **Adjustable font sizes.** All UI panel text sizes are configurable in preferences.

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
|          |  [Minimap]              [Z-Slider]   |             |
|          |                                      |             |
+----------+--------------------------------------+-------------+
| Status Bar                                                    |
+--------------------------------------------------------------+
```

### 2.2 Main Viewport

- Occupies the largest possible screen area (center region, expandable to full screen).
- Renders the current slide image with annotations overlaid.
- Contains floating widgets:
  - **Minimap/overview** (top-right corner by default, repositionable).
  - **Zoom level indicator** with slider (bottom-left).
  - **Z-stack slider** (left edge, visible only for multi-focal-plane slides).
  - **Rotation compass** (appears only when view is rotated).
- Supports split view: 1x1, 2x1, 1x2, or 2x2 grid for multi-slide comparison.
- Background color: neutral gray (#404040 in dark theme, #D0D0D0 in light theme) to avoid color perception bias when evaluating tissue stains.

### 2.3 Left Panel: Slide Tray / Case Panel

- **Default width:** 220px, resizable, collapsible.
- **Contents:**
  - Case header (accession number, patient ID -- hidden by default in privacy-sensitive environments --, clinical history snippet).
  - Slide thumbnail grid or list, each showing: thumbnail image, stain label, block ID.
  - Slides grouped by tissue block, sortable by stain type or scan date.
  - For large cases (100+ slides): virtual scrolling with sub-group headers and a search/filter field at the top.
- **Interactions:**
  - Single-click opens a slide in the main viewport.
  - Drag a slide onto a comparison panel to open it in split view.
  - Right-click for context menu: "Open in new window", "Open in split panel", "Show metadata", "Flag quality issue".
  - Hover shows an enlarged preview tooltip (300x300px) including the slide label/macro image (if available and not hidden by privacy settings).
- **Slide label image:** Each slide entry can display the physical slide label image captured by the scanner. **Default: hidden** (label images frequently contain patient name, DOB, and MRN). The user must explicitly enable label display in preferences or via a per-session toggle. Clicking a visible label thumbnail opens a larger popup for verification.
- **Collapsed state:** Shows only a vertical strip of mini-thumbnails (48px wide) for maximum viewport space.

### 2.4 Right Panel: Properties and Annotations

- **Default width:** 260px, resizable, collapsible.
- **Tabbed sub-panels:**

  1. **Annotations Tab:** List of all annotations on the current slide, grouped by layer. Each entry shows: icon (shape type), label text, classification color badge, author. Clicking an annotation in the list selects it on the slide and pans/zooms to fit it. Includes:
     - **Search box** at the top: filters by label text and classification. Supports type-ahead filtering.
     - **Sort options:** by position (top-to-bottom, left-to-right), creation date, classification, or type.
     - **Jump shortcuts:** Ctrl+] / Ctrl+[ to navigate to next/previous annotation sequentially.

  2. **Properties Tab:** Shows properties of the currently selected annotation. All fields editable inline:
     - Type (read-only), Label, Classification (dropdown with configurable taxonomy), Color (picker, default linked to classification), Line width (1-5 screen px), Fill opacity (0-100%, default 30% for regions), Measurements (read-only for measurement annotations), Author (read-only, derived from user identity), Created/Modified timestamps (read-only), Confidence (optional: Certain, Probable, Possible), Notes (multi-line text).

  3. **Metadata Tab:** Scanner metadata, slide-level metadata, case-level metadata. Read-only display with copy-to-clipboard buttons. Shows measurement calibration source and a warning badge when pixel-per-micrometer metadata is missing or manually overridden.

  4. **Layers Tab:** Annotation layer management. Toggle visibility (eye icon), lock/unlock (padlock icon), set opacity (slider), reorder (drag-and-drop). Create, rename, delete, merge layers. Active layer clearly indicated. Filter by author toggle.

  5. **Channels Tab** (appears only for multi-channel fluorescence slides): Per-channel visibility toggles, pseudo-color assignment (color picker per channel), per-channel brightness/contrast sliders, composite view toggle. Channels listed by name (e.g., DAPI, FITC, Cy3) with their assigned display color.

### 2.5 Main Toolbar

Horizontal toolbar below the menu bar, organized into logical groups separated by dividers:

```
[Navigation] | [Annotation Tools] | [View] | [Tools]

Navigation:  [Pan] [Zoom In] [Zoom Out] [Fit] [1:1]
Annotation:  [Select] [Rect] [Ellipse] [Freehand] [Point] [Arrow] [Ruler] [Area] [Text] [Angle]
View:        [Split 1x1] [Split 2x1] [Split 1x2] [Split 2x2] [Sync Lock]
Tools:       [Brightness/Contrast] [Snapshot] [Scale Bar Toggle]
```

- **Button style:** 24x24px icons with optional text labels (user-configurable: icons only, icons+text, text only).
- **Active tool highlight:** The currently active tool has a depressed/highlighted state.
- **Annotation tool defaults:** Right-click any annotation tool button to open a "Tool Defaults" popup for setting default classification and color. The toolbar button shows a small colored dot indicating the active default classification.
- **Annotation tool dropdown:** Each shape tool has a small dropdown arrow for variant selection (e.g., the Freehand tool dropdown offers "Polygon" and "Polyline").
- **Toolbar customization:** Users can show/hide toolbar groups and rearrange them via drag-and-drop.

### 2.6 Status Bar

Bottom bar showing:
- **Left:** Current magnification (e.g., "20.0x"), pixel coordinates under cursor (e.g., "X: 45230, Y: 12890"), active layer name.
- **Center:** Scale bar (physical units, e.g., "100 um"). A warning icon appears if measurement calibration metadata is missing.
- **Right:** Tile loading progress indicator (subtle spinner during tile loading), memory usage indicator, connection status for remote slides.
- **Tool hints:** When an annotation tool is active, the status bar shows contextual usage instructions (e.g., "Freehand Polygon: Click to place vertices, double-click to close").

### 2.7 Menu Structure

```
File
  Open Slide...              Ctrl+O
  Open Case...               Ctrl+Shift+O
  Recent Files               >
  Close Slide                Ctrl+W
  Close All                  Ctrl+Shift+W
  ---
  Import Annotations...
  Export Annotations...
  Export Annotations (Redacted)...
  Export Snapshot...          Ctrl+Shift+S
  ---
  Exit                       Alt+F4

Edit
  Undo                       Ctrl+Z
  Redo                       Ctrl+Y / Ctrl+Shift+Z
  ---
  Cut                        Ctrl+X
  Copy                       Ctrl+C
  Paste                      Ctrl+V
  Delete                     Delete
  Select All Annotations     Ctrl+A
  ---
  Find Annotation...         Ctrl+F
  Next Annotation            Ctrl+]
  Previous Annotation        Ctrl+[
  ---
  Preferences...             Ctrl+,

View
  Zoom In                    Ctrl++
  Zoom Out                   Ctrl+-
  Fit to Screen              Ctrl+0
  Actual Pixels (1:1)        Ctrl+1
  Magnification              > 2x, 4x, 10x, 20x, 40x
  ---
  Rotate Left                Ctrl+Shift+Left
  Rotate Right               Ctrl+Shift+Right
  Reset Rotation
  ---
  Full Screen                F11
  Presentation Mode          Ctrl+F11
  ---
  Left Panel                 Ctrl+L
  Right Panel                Ctrl+R
  Status Bar                 (toggle)
  Toolbar                    (toggle)
  Minimap                    Ctrl+M
  Scale Bar                  (toggle)
  ---
  Theme                      > Light, Dark, System

Slide
  Next Slide                 Page Down
  Previous Slide             Page Up
  ---
  Split View                 > 1x1, 2x1, 1x2, 2x2
  Synchronized Navigation    (toggle)
  ---
  Brightness/Contrast...     Ctrl+B
  Reset Display              Ctrl+Shift+R
  ---
  Calibration Override...
  Slide Information...

Annotations
  Select Tool                V
  Rectangle                  R
  Ellipse                    E
  Freehand Polygon           F
  Point Marker               P
  Arrow                      A
  Ruler                      M
  Area Measurement           Shift+M
  Text Label                 T
  Angle Measurement          G
  ---
  Lock Current Layer
  Show/Hide Current Layer
  ---
  Annotation Properties      Ctrl+I

Bookmarks
  Add Bookmark...            Ctrl+Shift+B
  Manage Bookmarks...
  ---
  (saved bookmarks listed here)

Window
  Save Layout As...
  Layouts                    > Single Monitor, Dual Monitor - Clinical,
                               Dual Monitor - Comparison, Presentation
  ---
  Open in New Window

Tools
  Snapshot                   Ctrl+Shift+S
  Annotation Summary
  ---
  Scale Bar Settings...
  Measurement Units          > Micrometers, Millimeters
  ---
  Shortcut Configuration...

Help
  User Guide                 F1
  Keyboard Shortcuts         Ctrl+/
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
| Ctrl+Scroll | Fine-grain zoom (smaller increments) |
| Double-click (left button) | Zoom in one step at click position |
| Pinch gesture (trackpad) | Continuous zoom centered on pinch center |
| Keyboard +/- | Zoom in/out centered on viewport center |
| Number keys 1-5 | Jump to 2x, 4x, 10x, 20x, 40x (matching microscope objectives) |

**Zoom behavior:**
- Continuous (smooth) zoom, not discrete steps. The zoom factor follows a logarithmic scale so that equal scroll increments produce equal perceptual magnification changes.
- Zoom range: approximately 0.25x (entire slide with margins) to the slide's native resolution (typically 40x or 60x).
- Zoom animation: 150ms ease-out transition for discrete zoom jumps (number keys, double-click). Scroll wheel zoom is instantaneous (no animation delay).
- Fit-to-screen (Ctrl+0): animated transition (300ms) to show the entire tissue region with 5% margin.

### 3.2 Pan Interaction Model

| Input | Action |
|-------|--------|
| Left mouse drag (Pan tool active or Space held) | Pan the viewport |
| Middle mouse drag (always) | Pan the viewport |
| Arrow keys | Pan by 10% of viewport in the arrow direction |
| Shift+Arrow keys | Pan by 50% of viewport |
| Touch drag (trackpad, two fingers) | Pan the viewport |

**Pan behavior:**
- Pan is always available via middle mouse button, regardless of the active tool. This allows panning while in annotation mode without switching tools.
- Holding Space temporarily activates pan mode (releases back to previous tool on Space release), following the Photoshop convention that many users know.
- Optional inertial panning (configurable in preferences): releasing a fast drag continues movement with friction-based deceleration. Clicking stops inertial movement immediately. Defaults to OFF, as some clinical users find it disorienting.
- Viewport is clamped to slide bounds with 10% overscroll padding.

### 3.3 Minimap / Overview Window

- **Position:** Floating widget, default top-right corner of the viewport. Draggable to any corner.
- **Size:** 180x180px default, resizable by dragging edges (range: 100-300px).
- **Content:** Shows the entire slide thumbnail with:
  - A blue rectangle indicating the current viewport extent.
  - Colored dots/regions showing annotation locations (optional, toggleable).
  - Tissue auto-detection highlights (dimming non-tissue background areas).
- **Interaction:**
  - Click on the minimap: jumps the main viewport to center on the clicked position.
  - Drag the blue rectangle: pans the main viewport correspondingly.
  - Scroll wheel on the minimap: zooms the main viewport.
- **Visibility:** Toggleable via View > Minimap or Ctrl+M. Auto-hides during Presentation Mode unless explicitly pinned.

### 3.4 Z-Stack Focus Control

For slides with multiple focal planes (common in cytology, thick tissue sections, and fluorescence imaging):

- **Z-level slider:** A small vertical slider on the left edge of the viewport, visible only when the current slide has multiple Z-planes.
- **Interaction:** Drag the slider to change the focal plane. The slider label shows the current Z-level index (e.g., "Z: 5/11").
- **Keyboard:** Ctrl+Up / Ctrl+Down to step through Z-levels one plane at a time.
- **Mouse:** Ctrl+Scroll wheel adjusts Z-level when the slide has multiple Z-planes, distinguished from regular scroll-wheel zoom.
- **Auto-hide:** The Z-slider is hidden for single-focal-plane slides to avoid UI clutter.
- **Extended focus:** An optional "Best Focus" button composites the sharpest regions from all Z-planes into a single extended depth-of-field view (computationally intensive; runs as a background task with a progress indicator).

### 3.5 View Rotation

Some tissue sections are scanned at an angle. Pathologists occasionally need to rotate the view to orient tissue conventionally:

- **Quick rotation:** Slide > Rotate Left / Rotate Right for 90-degree increments.
- **Keyboard:** Ctrl+Shift+Left / Ctrl+Shift+Right for 90-degree rotation.
- **Free rotation:** A rotation widget (circular handle) accessible from the toolbar or via Ctrl+Shift+Drag for arbitrary angle rotation.
- **Snap angles:** During free rotation, the view snaps to 0, 90, 180, 270 degrees when within 5 degrees of those values.
- **Rotation indicator:** When the view is rotated, a small compass indicator appears in the viewport corner showing the rotation angle. Clicking it resets to 0 degrees.
- **Coordinate system:** Rotation is display-only. Annotations and measurements remain in the slide's native coordinate system, unaffected by view rotation.

### 3.6 Zoom Level Indicator

- Floating widget in the bottom-left of the viewport (above the status bar).
- Displays current magnification as "20.0x" and a horizontal zoom slider.
- Clicking the magnification text opens a dropdown for direct numeric entry or preset selection (2x, 4x, 10x, 20x, 40x).
- The zoom slider allows click-drag for continuous zoom, or click on the track for jump-to zoom.

### 3.7 Navigation History

- **Back/Forward:** Ctrl+Left / Ctrl+Right (or browser-style mouse buttons 4/5) navigate through a history of viewport states.
- A viewport state is recorded when the user stops panning/zooming for 1 second, switches slides, or jumps to a bookmark.
- History depth: 100 entries per session.
- **Note:** Navigation history is separate from annotation undo/redo. Ctrl+Z undoes annotation edits; Ctrl+Left goes back to a previous viewport position. This distinction is communicated via onboarding hints on first use.

### 3.8 Bookmarks

- **Creating:** Ctrl+Shift+B saves the current state (slide, position, zoom, active layers) as a named bookmark.
- **Quick-save:** Shift+1 through Shift+9 save to numbered quick-bookmark slots (no dialog).
- **Quick-recall:** Alt+1 through Alt+9 jump to the corresponding quick-bookmark.
- **Bookmark panel:** Bookmarks listed in the Bookmarks menu and an optional bookmarks sidebar with thumbnail previews.
- **Persistence:** Bookmarks persist with the case file.

---

## 4. Annotation Tools

### 4.1 Tool Selection and Modes

The application operates in two primary modes:

1. **Navigation Mode (default):** Left-click-drag pans the viewport. The cursor is an open hand (grabbing hand while dragging).
2. **Annotation Mode:** An annotation tool is active. Left-click-drag creates or modifies annotations. Pan is available via middle mouse button or Space+drag.

Switching between modes:
- Press **V** to return to the Select/Navigate tool.
- Press a tool hotkey (**R**, **E**, **F**, **P**, **A**, **M**, **T**, **G**) to activate that annotation tool.
- Press **Escape** to deselect the current annotation and return to navigation mode.
- Configurable preference: **"Return to navigation mode after annotation"** (default OFF). When ON, the application automatically switches to Navigation Mode after each annotation is completed. Recommended for novice users and trainees.

### 4.2 Drawing Tools

#### Rectangle (R)
- Click-drag to define opposite corners.
- Hold **Shift** to constrain to a square.
- Visual feedback: dashed outline during creation, solid on release.

#### Ellipse (E)
- Click-drag to define the bounding box of the ellipse.
- Hold **Shift** to constrain to a circle.
- Hold **Alt** to draw from center outward.

#### Freehand Polygon (F)
- Click to place vertices; double-click or click the start vertex to close the polygon.
- Alternatively: hold-and-drag for freehand drawing (vertices auto-generated at configurable intervals).
- Press **Backspace** to remove the last placed vertex during creation.
- After creation: double-click the polygon to enter vertex editing mode (add, remove, or drag vertices).

#### Point Marker (P)
- Single-click to place a marker pin.
- Marker icon: filled circle with customizable color and optional label.
- Markers are always visible regardless of zoom level (rendered at a fixed screen-space size).

#### Arrow (A)
- Click-drag from tail to head.
- Arrowhead style: filled triangle (configurable size in preferences).

#### Ruler / Distance Measurement (M)
- Click-drag to define a line segment.
- Length is displayed alongside the ruler line in physical units (um or mm) using the slide's calibration metadata.
- The measurement label repositions automatically to avoid overlapping the ruler line.
- After completing a measurement, the annotation remains selected with its measurement label prominently displayed, even as the tool stays active for the next measurement. This allows the pathologist to read the result before proceeding.
- A warning icon appears on the measurement if the slide lacks pixel-per-micrometer calibration metadata.

#### Area Measurement (Shift+M)
- Same interaction as Freehand Polygon, but the area and perimeter are calculated and displayed inside the polygon in physical units (um^2 or mm^2).

#### Text Label (T)
- Click to place; an inline text editor appears immediately (no dialog).
- Press **Enter** to confirm, **Escape** to cancel.
- Font size is defined in screen-space points but the anchor position is in slide coordinates.

#### Angle Measurement (G)
- Click to place the vertex, drag to define the first ray, click to define the second ray.
- Angle value displayed at the vertex in degrees.

### 4.3 Annotation Selection and Editing

- **Select tool (V):** Click an annotation to select it. Shift+click to add to or remove from selection. Click on empty area to deselect all.
- **Rubber-band selection:** Click-drag on empty area creates a selection rectangle; all annotations fully inside are selected.
- **Selected annotation indicators:** Corner/edge handles for resize, a move cursor when hovering the interior.
- **Multi-select operations:** Move, delete, change color/classification, copy/paste apply to all selected annotations.
- **Right-click context menu** on an annotation:

```
Edit Properties...       Ctrl+I
Edit Vertices            (polygons only)
---
Bring to Front
Send to Back
---
Move to Layer            > (layer list)
---
Cut                      Ctrl+X
Copy                     Ctrl+C
Duplicate                Ctrl+D
Delete                   Delete
```

### 4.4 Quick Classification

For high-throughput annotation workflows (e.g., marking 50+ mitotic figures), navigating to the properties panel for each annotation is too slow. Two mechanisms address this:

1. **Tool-level default classification:** Right-click any annotation tool button in the toolbar to set a default classification and color. All subsequent annotations created with that tool inherit these defaults automatically. The toolbar button shows a small colored dot indicating the active default classification.

2. **Floating quick-classification bar:** After completing an annotation, a small floating bar appears adjacent to the new annotation for 3 seconds. It shows the most recently used classifications as colored buttons (e.g., [Tumor] [Normal] [Necrosis]). Single-click to assign. The bar auto-hides when the user starts drawing the next annotation. Disablable in preferences.

### 4.5 Annotation Property Panel

When an annotation is selected, the right panel's Properties tab shows:

| Field | Type | Description |
|-------|------|-------------|
| Type | Read-only | Rectangle, Ellipse, Polygon, Point, Arrow, Ruler, Area, Text, Angle |
| Label | Editable text | Short label displayed on the annotation |
| Classification | Dropdown | Configurable taxonomy (Tumor, Stroma, Necrosis, Normal, Artifact, custom). Color auto-assigned. |
| Color | Color picker | Default linked to classification, overridable |
| Line width | Slider (1-5 px) | Screen-space pixel width |
| Fill opacity | Slider (0-100%) | 0% = outline only, 30% = default for region highlights |
| Measurements | Read-only | Length, area, perimeter, angle (for measurement annotations) |
| Author | Read-only | Derived from authenticated user identity |
| Created / Modified | Read-only | Timestamps |
| Confidence | Optional dropdown | Certain, Probable, Possible (for research use) |
| Notes | Multi-line text | Extended comments |

### 4.6 Annotation Layers

- **Default layers:** "Diagnostic" (default for new annotations), "Teaching", "Research".
- **Layer panel controls:** Visibility toggle (eye icon), lock toggle (padlock icon), opacity slider per layer.
- **Layer operations:** Create, rename, delete, merge, reorder (drag-and-drop).
- **Filter by author:** A separate toggle in the annotation list filters annotations by author, independent of layers.
- **Active layer:** New annotations are created on the active (highlighted) layer. The active layer name is displayed in the status bar.

### 4.7 Measurement Calibration

- **Automatic:** Measurements derive their scale from the slide's pixel-per-micrometer metadata (provided by SlideIO from the scanner).
- **Manual override:** When metadata is missing or known to be incorrect, the user can set microns-per-pixel manually via Slide > Calibration Override. A warning badge appears on all measurements for that slide indicating manual calibration.
- **Display units:** Micrometers (um) for histology-scale measurements; millimeters (mm) for macro measurements. User-configurable in Tools > Measurement Units.
- **Scale bar:** Always-visible (toggleable) scale bar on the viewport showing current physical scale. Shows a warning icon when calibration data is absent.

### 4.8 Snap and Alignment Features

- **Snap-to-grid:** Optional grid overlay (configurable spacing in microns). Annotations snap to grid intersections when enabled.
- **Snap-to-annotation:** When dragging near an existing annotation edge or vertex, snap guides appear and the annotation aligns.
- **Smart guides:** Horizontal/vertical alignment guides appear when moving annotations into alignment with other annotations.
- **Toggle shortcuts:** Ctrl+Shift+G for grid snap, Ctrl+Shift+S for annotation snap.

---

## 5. Workflow Descriptions

### 5.1 Slide Review (Primary Diagnosis)

```
User Action                          UI Response
-----------                          -----------
1. Opens case from File menu         Left panel populates with slide list
   or drag-and-drop folder           and case metadata. First slide loads
                                     in viewport at fit-to-screen zoom.

2. Clicks a slide in the left panel  Slide loads with 200ms crossfade
                                     transition. Previous slide's viewport
                                     state is saved automatically.

3. Scrolls wheel to zoom to ~4x     Continuous zoom centered on cursor.
                                     Minimap updates to show viewport extent.

4. Middle-drags to pan across tissue Smooth panning at 60fps. Status bar
                                     shows coordinates and magnification.

5. Spots area of interest,           Smooth zoom. Progressive tile loading:
   scrolls to 20x                    blurry then sharp within 200ms.

6. Presses R, draws rectangle        Rectangle annotation created on
   around region                     active layer. Quick-classification bar
                                     appears briefly. Properties panel shows
                                     annotation details.

7. Types label: "Invasive carcinoma" Label text appears on the annotation.

8. Presses Page Down                 Next slide in case loads. Previous
                                     viewport and annotations auto-saved.

9. Reviews remaining slides          Same flow repeats.

10. Presses Ctrl+Shift+S             Snapshot captures current viewport
    for snapshot                      as PNG with annotations and scale bar.
```

### 5.2 Annotation Creation (High-Throughput)

```
1. Right-click the Point tool in toolbar
   -> Set default classification to "Mitotic Figure" (orange)

2. Press P to activate Point tool
   -> Cursor changes to crosshair
   -> Status bar: "Point Marker | Click to place"
   -> Toolbar shows orange dot on Point button

3. Click to place a point marker
   -> Orange point appears with "Mitotic Figure" classification
   -> Auto-save triggers (debounced)
   -> Tool remains active

4. Click to place another point
   -> Same classification applied automatically
   -> No need to visit properties panel

5. Repeat for all mitotic figures
   -> All points created with consistent classification

6. Press V to return to navigation mode
   -> All annotations visible in annotation list
```

### 5.3 Multi-Slide Comparison

```
1. Select View > Split View > 2x1 (or press Ctrl+2)
   -> Viewport splits into left and right panels
   -> Current slide in left panel
   -> Right panel shows "Drop a slide here" placeholder

2. Drag a slide from the case panel to the right viewport
   -> Right panel loads the dragged slide at fit-to-screen

3. Click "Sync Lock" button (or press Ctrl+Shift+L)
   -> Lock icon activates
   -> Both panels zoom and pan in synchronization

4. Scroll to zoom on either panel
   -> Both panels zoom together
   -> Corresponding regions shown side by side
   -> Essential for comparing H&E to immunostains

5. Click Sync Lock to unlock
   -> Panels navigate independently

6. Press Ctrl+1 to return to single-panel view
   -> Second panel closes, first panel expands
```

### 5.4 Tumor Board / Presentation

```
1. Open case, press Ctrl+F11 for Presentation Mode
   -> Menu bar, toolbar, and side panels hide
   -> All patient-identifying information hidden
   -> Viewport fills entire screen
   -> Floating mini-toolbar at bottom with essential controls
   -> Minimap visible if pinned

2. Navigate with mouse to demonstrate findings
   -> Clean, distraction-free, PHI-safe presentation

3. Press Escape or Ctrl+F11 to exit
   -> All panels restore to previous layout
   -> Patient info restored to normal visibility

4. For multi-monitor: undock case panel to secondary monitor
   -> Drag panel title bar off main window
   -> Panel becomes floating window on second monitor
   -> Slide viewport has full primary monitor
```

### 5.5 Teaching

```
1. Instructor opens a teaching case with pre-annotated slides
2. Sets the "Teaching" layer visible, "Diagnostic" layer hidden
   -> Students see guiding annotations but not diagnostic answers

3. Students create their own annotations on a "Student" layer
4. After exercise, instructor reveals the "Diagnostic" layer
   -> Students compare their annotations with the reference

5. Instructor captures snapshots at key teaching points
   -> Snapshots with annotations exported for lecture slides
```

### 5.6 Fluorescence Multi-Channel Review

```
1. Open a multi-channel fluorescence slide
   -> Channels Tab automatically appears in right panel
   -> Default composite view with DAPI (blue), FITC (green)

2. Toggle individual channel visibility
   -> Click eye icon next to each channel name
   -> Composite updates in real-time

3. Adjust per-channel brightness/contrast
   -> Drag sliders in Channels Tab
   -> Display adjustments are non-destructive

4. Assign pseudo-colors
   -> Click color swatch next to channel name
   -> Select from palette or enter custom color

5. Annotate on the composite view
   -> Annotations are independent of channel settings
```

---

## 6. Keyboard Shortcuts

All keyboard shortcuts are user-configurable via Tools > Shortcut Configuration. The application detects conflicts at startup and warns the user. The defaults listed below are tested on US keyboard layouts across Windows, macOS, and Linux. On macOS, Ctrl is replaced by Cmd where standard.

### 6.1 Navigation

| Shortcut | Action |
|----------|--------|
| Scroll Wheel | Zoom in/out at cursor |
| Middle Mouse Drag | Pan (always available) |
| Space + Left Drag | Pan (temporary, any mode) |
| Ctrl+0 | Fit to screen |
| Ctrl+1 | Actual pixels (1:1) |
| 1 / 2 / 3 / 4 / 5 | Jump to 2x / 4x / 10x / 20x / 40x |
| +/- | Zoom in/out (viewport center) |
| Arrow Keys | Pan 10% of viewport |
| Shift+Arrow Keys | Pan 50% of viewport |
| Ctrl+Left / Ctrl+Right | Navigation history back/forward |
| Page Up / Page Down | Previous / next slide in case |
| Home / End | First / last slide in case |
| F11 | Toggle full screen |
| Ctrl+F11 | Toggle Presentation Mode |
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
| Ctrl+Z | Undo (annotation operations only) |
| Ctrl+Y / Ctrl+Shift+Z | Redo |
| Ctrl+X | Cut annotation(s) |
| Ctrl+C | Copy annotation(s) |
| Ctrl+V | Paste annotation(s) |
| Ctrl+D | Duplicate annotation(s) |
| Delete / Backspace | Delete selected annotation(s) |
| Ctrl+A | Select all annotations on visible layers |
| Ctrl+I | Show annotation properties |
| Ctrl+F | Search annotations |
| Ctrl+] / Ctrl+[ | Jump to next / previous annotation |

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
| Shift+1 through Shift+9 | Save to quick bookmark slot |
| Alt+1 through Alt+9 | Recall quick bookmark |
| Ctrl+/ | Show keyboard shortcut reference |
| Ctrl+, | Open preferences |

### 6.6 Modifier Keys During Annotation

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
Zoom to...              > 2x, 4x, 10x, 20x, 40x
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
Open in Split Panel     > Left, Right, Top-Left, Top-Right
---
Show Metadata
Show Label Image        (if available and not hidden by privacy)
Flag Quality Issue      > Focus, Staining, Artifact, Other
```

### 7.2 Drag-and-Drop

| Source | Target | Action |
|--------|--------|--------|
| Slide file (OS file explorer) | Application window | Open slide or create case |
| Slide thumbnail (left panel) | Main viewport | Open slide in viewport |
| Slide thumbnail (left panel) | Split panel | Open slide in that panel |
| Annotation (viewport) | Layer (layer panel) | Move annotation to that layer |
| Annotation file (.json) | Application window | Import annotations |

### 7.3 Multi-Select Patterns

- **Shift+Click:** Toggle individual annotation in selection.
- **Ctrl+A:** Select all annotations on visible, unlocked layers.
- **Rubber-band:** Click-drag on empty area (with Select tool) creates a selection rectangle; all annotations fully inside are selected.
- **Annotation list:** Click, Shift+Click (range), Ctrl+Click (toggle) in the annotation list panel.
- **Batch operations on multi-select:** Delete, move to layer, change classification, change color, copy, cut.

### 7.4 Undo/Redo

- **Scope:** Undo/redo operates per-slide. Switching slides switches the undo stack.
- **Tracked operations:** Create, delete, move, resize, reshape, property change, paste, layer change.
- **Not tracked:** Navigation (pan/zoom), panel visibility, display adjustments (brightness/contrast, rotation). Navigation has its own separate history system (Ctrl+Left/Right).
- **Stack depth:** 50 levels minimum, configurable up to 200 in preferences.
- **Visual feedback:** Brief toast notification near the toolbar showing the undone/redone action (e.g., "Undo: Delete Rectangle"), visible for 2 seconds.

### 7.5 Tooltips and Onboarding

- **Toolbar buttons:** Tooltip on hover showing tool name + keyboard shortcut (e.g., "Freehand Polygon (F)").
- **Status bar hints:** When a tool is active, the status bar shows contextual usage instructions.
- **First-run onboarding:** On first launch, subtle coach marks highlight key UI areas:
  1. "Scroll to zoom, middle-click to pan"
  2. "Annotation tools stay active -- press V or Escape to return to navigation"
  3. "Ctrl+Z undoes annotations, Ctrl+Left goes back in navigation history"
  These are dismissible, non-blocking, and never shown again after dismissal.

---

## 8. Multi-Monitor Support

### 8.1 Detachable Panels

All side panels can be detached from the main window:

- **Undock:** Drag the panel's title bar away from the main window, or double-click the title bar.
- **Redock:** Drag the floating panel back to a dock zone (left/right edges of the main window show drop highlights).
- **Independent windows:** Floating panels are proper OS windows that can be minimized, resized, and moved to any monitor.

Detachable panels:
- Left panel (slide tray / case panel)
- Right panel (annotations / properties / metadata / layers / channels)
- Bookmark panel
- Minimap (as a standalone floating window)

### 8.2 Multi-Window Viewing

- **New window:** Window > Open in New Window, or right-click a slide > "Open in New Window".
- **Each window** is an independent viewer instance sharing the same case data and annotation store.
- **Annotation synchronization:** Changes to annotations in one window are reflected in all windows viewing the same slide (within the same process). File locking prevents external concurrent write conflicts.
- **Use case:** One monitor shows the full slide viewport, the other shows properties panels and the annotation list. Or two monitors show two different slides from the same case for reference.

### 8.3 Saved Layouts

- **Save layout:** Window > Save Layout As... saves positions, sizes, panel docking state, split view configuration, and monitor assignment.
- **Preset layouts:**
  - "Single Monitor" -- all panels docked, no split view.
  - "Dual Monitor - Clinical" -- full viewport on monitor 1, all panels on monitor 2.
  - "Dual Monitor - Comparison" -- split view on monitor 1, case panel on monitor 2.
  - "Presentation" -- full screen viewport, no panels, patient info hidden.
- **Restore layout:** Window > Layouts > (layout name).
- **Auto-restore:** The last used layout is restored on application launch.

---

## 9. Performance Perception

### 9.1 Progressive Tile Loading

When zooming into a new region, the viewer must maintain visual continuity:

1. **Immediate:** Display the current (lower-resolution) tile data scaled up (blurry but immediate). The user always sees image content, never gray or blank areas.
2. **Fast (~100-200ms):** Replace with the correct resolution tile as it decodes.
3. **Progressive refinement:** If the target tile is still loading, interpolate from the nearest available resolution level.

The transition from blurry to sharp uses a smooth 100ms crossfade, not a jarring pop.

### 9.2 Prefetching Strategy

- **Spatial prefetch:** Tiles adjacent to the current viewport (1 tile ring) are prefetched in the background.
- **Zoom prefetch:** One zoom level above and below the current level is prefetched for the current viewport area.
- **Predictive prefetch:** If the user is panning in a direction, tiles ahead of the pan direction are prioritized.
- **Request coalescing:** During rapid panning, tile requests for regions that are no longer visible are cancelled. A client-side rate limit (configurable, default 60 requests/second) prevents overwhelming remote tile servers.
- **UX requirement:** Prefetching never causes UI jank. Tile decoding occurs on background threads with lower priority than current viewport tiles.

### 9.3 Loading Indicators

- **Tile-level:** No per-tile loading indicator (too visually noisy). The blurry-to-sharp progressive refinement serves as the indicator.
- **Slide-level:** When opening a slide, a centered spinner with "Loading [slide name]..." appears over the viewport for the initial load.
- **Status bar:** A subtle progress indicator (thin progress bar or spinner icon) in the status bar shows when background tile loading is active. It disappears when all visible tiles are loaded.
- **Remote slides:** For remote slides with higher latency, a semi-transparent overlay with a progress bar and estimated time appears if loading exceeds 3 seconds.

### 9.4 Smooth Transitions

- **Slide switching:** 200ms crossfade between previous and new slide.
- **Fit-to-screen / zoom-to-bookmark:** Animated zoom+pan transition (300ms ease-out).
- **Panel show/hide:** 150ms slide animation for panel collapse/expand.
- **Split view resize:** Live resize with smooth viewport adjustment.
- **All animations are cancellable:** Starting a new navigation action immediately cancels any in-flight animation.

### 9.5 Responsiveness Guarantees

| Metric | Target |
|--------|--------|
| Input latency (mouse/keyboard to screen) | < 16ms (one frame at 60fps) |
| Pan/zoom frame rate | 60fps sustained |
| Slide open (local file) | < 2 seconds to first view |
| Slide open (remote) | < 5 seconds to first view |
| Pan to cached region | < 200ms |
| Zoom one level to sharp view | < 300ms |
| Slide switch within case | < 1 second |
| Annotation creation response | < 100ms |
| Freehand drawing | Sampled at full input rate, rendered immediately. Line smoothing applied on mouse release. |

If the GPU cannot maintain 60fps, the renderer reduces tile resolution temporarily rather than dropping frames.

---

## 10. Theming and Visual Design

### 10.1 Color Themes

- **Light theme:** White/light gray chrome, dark text. Best for well-lit clinical environments.
- **Dark theme:** Dark gray chrome (#2D2D2D background, #E0E0E0 text). Best for tumor board rooms and dimmed environments. Reduces eye strain during long sessions.
- **System theme:** Follow the OS light/dark mode setting automatically.
- **Viewport background:** Always neutral gray regardless of theme to avoid color perception bias when evaluating tissue stains.

### 10.2 Annotation Default Colors by Classification

| Classification | Color | Hex | Rationale |
|---------------|-------|-----|-----------|
| Tumor | Orange | #E67E22 | High visibility, colorblind-safe |
| Stroma | Blue | #3498DB | Distinct from warm tissue tones |
| Necrosis | Purple | #9B59B6 | Distinct from both tissue and other annotations |
| Normal | Green | #2ECC71 | "Safe" / normal connotation |
| Artifact | Red | #E74C3C | Warning / problem indicator |
| Unclassified | Gray | #95A5A6 | Neutral default |

All colors are user-configurable in preferences. The default palette is designed to be distinguishable by users with common forms of color vision deficiency (deuteranopia, protanopia).

### 10.3 Icon Design

- **Style:** Simple, outlined (not filled), monochrome icons that adapt to the active theme.
- **Size:** 24x24px at 1x DPI, provided as SVGs for crisp rendering at all display scales.
- **Active state:** Filled variant or accent-color background to indicate the selected tool.

---

## 11. Error States and Edge Cases

### 11.1 Slide Load Failure

- Display a placeholder in the viewport: centered error icon + message "Unable to open [filename]: [error description]".
- The slide entry in the case panel shows a warning badge.
- Other slides in the case remain accessible and functional.

### 11.2 Corrupt Tiles

- If individual tiles fail to decode, display a subtle hatched pattern in that tile area with a small warning icon.
- Surrounding tiles render normally.
- The status bar shows "N tiles failed to load" with a clickable link to details.

### 11.3 Network Interruption (Remote Slides)

- Display cached tiles at whatever resolution is available.
- A banner at the top of the viewport: "Connection lost. Showing cached data. Reconnecting..."
- Auto-retry connection every 5 seconds with exponential backoff. On reconnection, resume tile loading seamlessly.
- Annotations continue to work locally and sync on reconnection.

### 11.4 Insufficient Memory

- As memory usage approaches the configured limit, display a status bar warning: "High memory usage (3.5/4.0 GB)".
- The tile cache evicts aggressively. The user may notice more blurry-to-sharp transitions but the application does not crash.
- On 4K displays or multi-slide comparison, the default memory budget is automatically increased.

### 11.5 Missing Calibration Metadata

- When a slide lacks pixel-per-micrometer metadata, all measurement annotations display a warning icon.
- The scale bar shows "Uncalibrated" instead of physical units.
- A notification prompts the user to set manual calibration via Slide > Calibration Override.
- The annotation file records the calibration source (metadata vs. manual) for reproducibility.

---

## 12. Privacy and Presentation Mode

### 12.1 Privacy Controls

Patient-identifying information appears in several places: the case panel header (patient ID, accession number), slide label/macro images, and potentially in annotation text. The application provides layered privacy controls:

- **Slide label images:** Default to **hidden**. Must be explicitly enabled in preferences or via a per-session toggle.
- **Patient identifiers in case panel:** Displayed by default for clinical workflows, but hideable via a "Hide Patient Info" toggle (accessible from the case panel header or View menu).
- **Annotation export redaction:** File > Export Annotations (Redacted) strips patient identifiers, author names, and case metadata from the exported annotation file before saving.

### 12.2 Presentation Mode (Ctrl+F11)

A dedicated mode for tumor board reviews, teaching sessions, and any context where the screen may be visible to unauthorized viewers:

- All patient-identifying information is automatically hidden (case header, label images, patient-related metadata).
- Menu bar, toolbar, and side panels are hidden for a clean, full-screen presentation.
- A floating mini-toolbar with essential navigation controls appears at the bottom edge.
- Minimap remains visible if pinned.
- Exiting Presentation Mode restores the full layout and patient information visibility.

---

*This document defines the complete user interface design for SlideIO Viewer, informed by clinical pathology requirements (Phase 1), domain expert review, system architecture constraints (Phase 3), and critical review findings (Phase 5). It serves as the specification for UI implementation alongside Document 1 (Software Requirements Specification) and Document 3 (Software Architecture and Design).*
