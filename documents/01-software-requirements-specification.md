# Software Requirements Specification

**Project:** SlideIO Viewer
**Version:** 1.0
**Date:** 2026-03-13
**Status:** Final

---

## Table of Contents

1. [Overview](#1-overview)
2. [User Personas](#2-user-personas)
3. [Clinical Workflows](#3-clinical-workflows)
4. [Functional Requirements](#4-functional-requirements)
5. [Annotation Requirements](#5-annotation-requirements)
6. [Image Handling Requirements](#6-image-handling-requirements)
7. [Remote Access Requirements](#7-remote-access-requirements)
8. [Non-Functional Requirements](#8-non-functional-requirements)
9. [Security and Compliance Requirements](#9-security-and-compliance-requirements)
10. [Open Items and Future Scope](#10-open-items-and-future-scope)

---

## 1. Overview

### 1.1 Purpose

SlideIO Viewer is a cross-platform desktop application for viewing, navigating, and annotating digital pathology whole-slide images (WSI). It serves clinical pathologists performing primary diagnosis, researchers conducting quantitative tissue analysis, lab technicians verifying scan quality, and trainees learning pathology.

### 1.2 Scope

The application provides:
- High-performance viewing of gigapixel-scale pathology images with smooth pan/zoom (60 fps target).
- Multi-format slide support via the SlideIO library (SVS, NDPI, SCN, BIF, MRXS, iSyntax, TIFF, DICOM WSI, CZI, OME-TIFF).
- Comprehensive annotation tools for marking regions, measuring structures, and classifying findings.
- Case management for grouping slides with clinical metadata.
- Multi-slide comparison with synchronized navigation.
- Local and remote slide access.
- Plugin architecture for extensibility.

### 1.3 Target Platforms

- Windows 10/11 (x64)
- macOS 12+ (Intel and Apple Silicon)
- Ubuntu 22.04+ and major Linux distributions (x64)

### 1.4 Key Dependencies

- **SlideIO** -- multi-format slide access library
- **Qt 6** -- cross-platform UI framework
- **OpenGL 3.3+** -- GPU-accelerated tile rendering

### 1.5 Definitions

| Term | Definition |
|------|-----------|
| WSI | Whole-slide image -- a digitized tissue slide, typically 50K-200K pixels on each axis |
| Tile | A fixed-size sub-region of a slide at a given resolution level, typically 256x256 pixels |
| Pyramid | A multi-resolution representation of a slide, with each level at half the resolution of the one below |
| Annotation | A user-created geometric shape, measurement, or text label overlaid on a slide |
| Case | A logical grouping of slides associated with a patient or study, with metadata |
| PHI | Protected Health Information -- data that identifies a patient, regulated by HIPAA/GDPR |

---

## 2. User Personas

### 2.1 Clinical Pathologist

- **Profile:** Board-certified physician, 30-65 years old, high caseload pressure (40-80 slides/day), variable computer literacy.
- **Goals:** Accurate diagnosis with minimal time per case, efficient navigation, reliable annotations.
- **Pain points:** Slow load times break diagnostic concentration. Unfamiliar UI patterns cause errors. Too many clicks for routine tasks.
- **Environment:** Dual-monitor workstation in a hospital. One screen for the viewer, one for the Laboratory Information System (LIS).
- **Usage:** 6-10 hours/day in repetitive diagnostic workflows. Values keyboard shortcuts and muscle memory.

### 2.2 Lab Technician / Histotechnologist

- **Profile:** Operates scanners, manages slide storage, performs quality checks.
- **Goals:** Verify scan quality, organize slides into cases, manage file storage.
- **Pain points:** Needs lightweight browsing without full diagnostic tools.
- **Usage:** Intermittent use throughout the day, brief interactions per slide.

### 2.3 Research Scientist

- **Profile:** PhD-level researcher, comfortable with software, needs quantitative tools.
- **Goals:** Precise measurements, reproducible annotations, data export for analysis pipelines.
- **Pain points:** Imprecise measurement tools, inability to export annotation data programmatically.
- **Usage:** Extended sessions analyzing specific tissue features across multiple slides. May work with large cohorts (50-500 slides).

### 2.4 Pathology Trainee / Student

- **Profile:** Medical student or pathology resident, learning to identify tissue patterns.
- **Goals:** Practice slide review, learn from annotated teaching sets, build diagnostic skills.
- **Pain points:** Overwhelmed by complex interfaces, needs guided navigation.
- **Usage:** Study sessions of 1-3 hours, working through curated slide sets.

---

## 3. Clinical Workflows

### 3.1 Primary Diagnosis

1. Pathologist receives case notification from LIS (external to viewer).
2. Opens the case in the viewer. The case panel populates with slide list and thumbnails.
3. Clicks the first slide; it loads at a fit-to-screen overview.
4. Systematically scans tissue at 4x-10x using pan/zoom. Uses the minimap for orientation.
5. Zooms to 20x-40x on areas of interest for cellular detail examination.
6. Annotates regions of interest with geometric shapes and text labels.
7. Switches to the next slide in the case. The viewer remembers the viewport position on the previous slide.
8. Optionally views two slides side-by-side (e.g., H&E next to Ki-67 immunostain).
9. Captures viewport snapshots for inclusion in the diagnostic report.
10. Annotations auto-save and persist with the slide.

**Performance constraint:** The entire workflow must feel responsive. Slide load < 2s (local), pan/zoom at 60 fps, annotation creation immediate (< 100ms).

### 3.2 Second Opinion / Consultation

1. Consulting pathologist opens shared slides.
2. Reviews the referring pathologist's annotations (view annotations from other users).
3. Toggles annotation visibility by author.
4. Adds their own annotations on a separate layer.
5. Uses bookmarks/snapshots shared by the referring pathologist to navigate to regions of concern.

### 3.3 Tumor Board Review

1. Pathologist opens case and enters full-screen mode (presentation-friendly UI).
2. Navigates and demonstrates findings on a large/projected display.
3. Shows side-by-side slide comparison (H&E next to immunohistochemistry).
4. All patient-identifying information (label images, patient IDs) is hidden in presentation mode.

### 3.4 Teaching and Training

1. Instructor opens a teaching case with pre-annotated slides.
2. Sets the "Teaching" annotation layer to visible, "Diagnostic" answers layer to hidden.
3. Students navigate independently and create their own annotations on a "Student" layer.
4. After the exercise, instructor reveals the "Diagnostic" layer for comparison.
5. Instructor captures snapshots at key teaching points for lecture materials.

### 3.5 Research Analysis

1. Researcher opens slides from a study cohort.
2. Creates precise measurement annotations (distance, area, cell counts).
3. Classifies annotations using a configurable taxonomy.
4. Exports annotation data (coordinates, measurements, classifications) in standard formats (GeoJSON, CSV).
5. May work with AI-generated annotation overlays from plugins.

### 3.6 Quality Assurance

1. Lab manager browses recently digitized slides.
2. Opens each slide briefly to verify scan quality (focus, staining, artifacts).
3. Flags quality issues using a quality categories system.
4. Reviews scanner metadata (scan date, resolution, focus quality metrics).

### 3.7 Case Assembly

1. Technician imports scanned slide files from a local directory or network share.
2. Groups slides into a case (patient + accession number).
3. Associates metadata: stain type, tissue type, block/slide identifiers.
4. Saves the case definition for later review by the pathologist.

---

## 4. Functional Requirements

### 4.1 Slide Viewing

| ID | Requirement | Priority |
|----|------------|----------|
| FR-VIEW-01 | The application shall open WSI files from local disk and network shares. | Must |
| FR-VIEW-02 | The application shall display slides using the image pyramid, selecting the appropriate resolution level for the current zoom. | Must |
| FR-VIEW-03 | The application shall support continuous (smooth) pan and zoom, not discrete steps. | Must |
| FR-VIEW-04 | Zoom shall center on the cursor position (mouse/trackpad) or viewport center (keyboard). | Must |
| FR-VIEW-05 | The application shall display a minimap showing the full slide with the current viewport extent. The minimap shall be interactive (click to navigate, drag to pan). | Must |
| FR-VIEW-06 | The application shall display the current magnification level (e.g., "20.0x") and physical scale bar. | Must |
| FR-VIEW-07 | The application shall display pixel coordinates under the cursor in the status bar. | Should |
| FR-VIEW-08 | The application shall support view rotation in 90-degree increments and free rotation. Rotation is display-only; annotations remain in native slide coordinates. | Should |
| FR-VIEW-09 | The application shall support Z-stack navigation for multi-focal-plane slides, with a Z-level slider visible only when applicable. Pathologists routinely adjust focus on cytology and thick-section slides. | Must |
| FR-VIEW-10 | The application shall support split-view mode (2x1, 1x2, 2x2) for multi-slide comparison with optional synchronized navigation. | Must |

### 4.2 Case Management

| ID | Requirement | Priority |
|----|------------|----------|
| FR-CASE-01 | The application shall support creating, opening, and saving cases as case manifest files (JSON format). | Must |
| FR-CASE-02 | A case shall contain references to one or more slide files, case-level metadata, and bookmarks. | Must |
| FR-CASE-03 | The case panel (left sidebar) shall display slide thumbnails grouped by tissue block, sortable by stain type or scan date. | Must |
| FR-CASE-04 | Single-click on a slide in the case panel shall open it in the main viewport. | Must |
| FR-CASE-05 | The application shall remember the viewport state (position, zoom) per slide when switching between slides in a case. | Must |
| FR-CASE-06 | Slide label/macro images shall be hidden by default and require explicit user action to reveal, to prevent unintended PHI exposure. | Must |
| FR-CASE-07 | The application shall support drag-and-drop of slide files from the OS file explorer to open slides or create cases. | Should |
| FR-CASE-08 | The case manifest shall store slide references using both absolute and relative file paths for portability. | Must |

### 4.3 Navigation

| ID | Requirement | Priority |
|----|------------|----------|
| FR-NAV-01 | The application shall support navigation history (back/forward) for revisiting previous viewport positions. | Must |
| FR-NAV-02 | The application shall support named bookmarks that save viewport state (position, zoom, active layers) and persist with the case. | Must |
| FR-NAV-03 | Quick-bookmark slots (Shift+1 through Shift+9 to save, Alt+1 through Alt+9 to recall) shall be supported for rapid workflow. | Should |
| FR-NAV-04 | Number keys 1-5 shall jump to preset magnifications (2x, 4x, 10x, 20x, 40x) matching microscope objectives. | Must |
| FR-NAV-05 | Panning shall be available via middle mouse button at all times, regardless of the active tool. | Must |
| FR-NAV-06 | Space+left-drag shall temporarily activate pan mode, returning to the previous tool on release. | Must |

### 4.4 User Interface

| ID | Requirement | Priority |
|----|------------|----------|
| FR-UI-01 | The application shall provide a main viewport occupying the largest possible screen area (85%+ in default layout). | Must |
| FR-UI-02 | Side panels (slide tray, annotation list, properties, metadata, layers) shall be resizable, collapsible, and detachable for multi-monitor use. | Must |
| FR-UI-03 | The application shall support full-screen mode for presentation. | Must |
| FR-UI-04 | Light and dark color themes shall be available, plus a "System" option that follows OS settings. | Must |
| FR-UI-05 | The viewport background shall be a neutral gray regardless of theme, to avoid color perception bias. | Must |
| FR-UI-06 | The application shall support saving and restoring window layouts (panel positions, split-view state). | Should |
| FR-UI-07 | All keyboard shortcuts shall be user-configurable in preferences. | Should |
| FR-UI-08 | A presentation mode shall automatically hide all patient-identifying information (labels, patient IDs, case metadata). | Must |
| FR-UI-09 | The application shall provide a first-run onboarding with subtle coach marks for key UI areas (dismissible). | Should |

### 4.5 User Identity

| ID | Requirement | Priority |
|----|------------|----------|
| FR-USER-01 | The application shall require a user identity (username or initials) configured in preferences before annotations can be created. | Must |
| FR-USER-02 | The annotation author field shall be derived from the configured user identity and shall not be user-editable per annotation. | Must |
| FR-USER-03 | For institutional deployments, the application should support LDAP/Active Directory or SSO integration via a plugin. | Should |

### 4.6 Snapshot and Export

| ID | Requirement | Priority |
|----|------------|----------|
| FR-SNAP-01 | The application shall capture viewport snapshots as PNG/JPEG with annotations and scale bar. | Must |
| FR-SNAP-02 | The application shall generate a text summary of all annotations (labels, measurements, classifications) for report inclusion. | Should |

---

## 5. Annotation Requirements

### 5.1 Geometric Annotation Types

| ID | Type | Description | Priority |
|----|------|-------------|----------|
| FR-ANN-01 | Rectangle | Axis-aligned bounding box via click-drag. Shift constrains to square. | Must |
| FR-ANN-02 | Ellipse | Bounding box defined via click-drag. Shift constrains to circle. Alt draws from center. | Must |
| FR-ANN-03 | Freehand Polygon | Click to place vertices, double-click to close. Supports hold-and-drag freehand drawing. | Must |
| FR-ANN-04 | Freehand Line | Open path via click-and-drag or vertex placement. | Must |
| FR-ANN-05 | Point Marker | Single-click placement. Always visible regardless of zoom level (screen-space size). | Must |
| FR-ANN-06 | Arrow | Click-drag from tail to head. | Must |
| FR-ANN-07 | Ruler (Distance) | Line segment with length display in physical units (um or mm) using slide calibration. | Must |
| FR-ANN-08 | Area Measurement | Closed polygon with area display in physical units. | Must |
| FR-ANN-09 | Angle Measurement | Two-ray angle measured at a vertex, displayed in degrees. | Should |
| FR-ANN-10 | Text Label | Click to place, inline text editor (no dialog). | Must |

### 5.2 Annotation Properties

| ID | Requirement | Priority |
|----|------------|----------|
| FR-ANN-11 | Each annotation shall have: color, line width (1-5 screen pixels), fill opacity (0-100%), label text, classification category, and notes. | Must |
| FR-ANN-12 | Each annotation shall automatically record: author (from user identity), creation timestamp, last-modified timestamp. | Must |
| FR-ANN-13 | Classification categories shall be user-configurable (default: Tumor, Stroma, Necrosis, Normal, Artifact, Unclassified). | Must |
| FR-ANN-14 | An optional confidence level field (Certain, Probable, Possible) shall be available for research use. | Should |
| FR-ANN-15 | Annotations shall use a colorblind-safe default palette (blue, orange, yellow, purple, cyan -- avoiding red/green only distinctions). | Must |

### 5.3 Annotation Interaction

| ID | Requirement | Priority |
|----|------------|----------|
| FR-ANN-16 | Click to select, Shift+click for multi-select, rubber-band selection for area selection. | Must |
| FR-ANN-17 | Move, resize, reshape (vertex editing) via drag handles. | Must |
| FR-ANN-18 | Undo/redo stack with minimum 50 levels, per-slide. | Must |
| FR-ANN-19 | Copy/paste annotations within a slide or across slides. | Must |
| FR-ANN-20 | Right-click context menu for annotation operations (properties, layer assignment, z-order, delete). | Must |
| FR-ANN-21 | A quick-classification bar shall appear after annotation creation, showing recently used classifications for one-click assignment. | Should |
| FR-ANN-22 | Each annotation tool shall support configurable default classification and color via right-click on the toolbar button. | Should |

### 5.4 Annotation Layers

| ID | Requirement | Priority |
|----|------------|----------|
| FR-ANN-23 | Annotations shall be organized into named layers with visibility toggle, lock toggle, and opacity control. | Must |
| FR-ANN-24 | Default layers: "Diagnostic", "Teaching", "Research". Users can create, rename, delete, and reorder layers. | Must |
| FR-ANN-25 | Annotation list shall support filtering by author independent of layers. | Must |
| FR-ANN-26 | The annotation list panel shall provide a search box for filtering by label text and classification, and sort options (by position, date, classification). | Must |
| FR-ANN-27 | A "jump to next/previous annotation" keyboard shortcut shall support sequential review of annotations across the slide. | Should |

### 5.5 Annotation Storage

| ID | Requirement | Priority |
|----|------------|----------|
| FR-ANN-27 | Annotations shall be stored in JSON format (GeoJSON-based), separate from slide image files. | Must |
| FR-ANN-28 | All geometry coordinates shall be in pixel units at pyramid level 0 (highest resolution). | Must |
| FR-ANN-29 | Annotation files shall use a content-derived slide identifier (hash of file path, size, and key metadata) to prevent misassociation when slide filenames collide. | Must |
| FR-ANN-30 | The annotation file format shall include a version number for forward compatibility. | Must |
| FR-ANN-31 | Auto-save shall trigger on every annotation completion (debounced to 2 seconds of inactivity) and periodically every 30 seconds. | Must |
| FR-ANN-32 | File locking (advisory) shall be used on annotation files to prevent data loss from concurrent access by multiple processes. | Must |
| FR-ANN-33 | Import shall be supported from QuPath GeoJSON and ASAP XML formats. | Should |
| FR-ANN-34 | Export shall be supported to GeoJSON and CSV (measurements). | Must |

### 5.6 Measurement Calibration

| ID | Requirement | Priority |
|----|------------|----------|
| FR-MEAS-01 | Measurements shall derive scale from the slide's microns-per-pixel metadata (via SlideIO). | Must |
| FR-MEAS-02 | Display units shall be user-configurable: micrometers or millimeters. | Must |
| FR-MEAS-03 | A scale bar shall be visible on the viewport, showing the current physical scale. | Must |
| FR-MEAS-04 | A manual calibration override shall allow the user to set microns-per-pixel when metadata is missing or incorrect. | Must |
| FR-MEAS-05 | A warning badge shall be displayed on measurements when calibration metadata is absent. | Must |

### 5.7 Application Log

| ID | Requirement | Priority |
|----|------------|----------|
| FR-LOG-01 | The application shall provide a log panel that displays application log messages in real time. | Must |
| FR-LOG-02 | The log panel shall be hidden by default and opened on user request (via menu or keyboard shortcut). | Must |
| FR-LOG-03 | The log panel shall display log entries with timestamp, severity level, source component, and message text. | Must |
| FR-LOG-04 | The log panel shall support filtering by severity level (debug, info, warning, error). | Should |
| FR-LOG-05 | The log panel shall support text search within displayed log entries. | Should |
| FR-LOG-06 | The user shall be able to clear the log panel contents. | Should |
| FR-LOG-07 | The user shall be able to copy selected log entries to the clipboard. | Should |

---

## 6. Image Handling Requirements

### 6.1 Supported Formats

| ID | Requirement | Priority |
|----|------------|----------|
| FR-IMG-01 | The application shall support all slide formats accessible via SlideIO, including: SVS, NDPI, SCN, BIF, MRXS, iSyntax, TIFF/BigTIFF, DICOM WSI, CZI, and OME-TIFF. | Must |
| FR-IMG-02 | The application shall be format-agnostic at the application level; all format details are encapsulated behind SlideIO. | Must |

### 6.2 Image Characteristics

| ID | Requirement | Priority |
|----|------------|----------|
| FR-IMG-03 | The application shall not impose any restriction on image dimensions. It shall handle slides of arbitrary size, including those exceeding 200,000 x 200,000 pixels. | Must |
| FR-IMG-04 | The application shall support the following pixel data types: 8-bit unsigned (brightfield RGB), 16-bit unsigned and 16-bit signed integers, 32-bit unsigned and 32-bit signed integers, and 32-bit floating point. Multi-channel fluorescence images shall be supported for all listed data types. | Must |
| FR-IMG-05 | For multi-channel fluorescence, per-channel visibility, pseudo-color assignment, and brightness/contrast controls shall be available. A dedicated Channels panel shall appear automatically for multi-channel slides. | Must |
| FR-IMG-06 | The application shall not impose any restriction on slide file size. It shall handle files of arbitrary size, including those exceeding 10 GB. | Must |

### 6.3 Image Pyramid and Tiling

| ID | Requirement | Priority |
|----|------------|----------|
| FR-IMG-07 | The application shall request tiles at the pyramid level appropriate for the current zoom and display. | Must |
| FR-IMG-08 | Tile decoding shall be asynchronous on background threads, never blocking the UI thread. | Must |
| FR-IMG-09 | Lower-resolution tiles shall be displayed as placeholders while higher-resolution tiles load (progressive refinement with smooth crossfade). | Must |
| FR-IMG-10 | The tile size should match the slide's native tile grid where feasible, falling back to 256x256 when native tile size is unavailable or impractical. | Should |
| FR-IMG-11 | Tiles adjacent to the viewport and at neighboring zoom levels shall be prefetched in the background. | Must |

### 6.4 Tile Cache

| ID | Requirement | Priority |
|----|------------|----------|
| FR-IMG-12 | An in-memory LRU tile cache shall be maintained with a configurable memory budget (default: 25% of available RAM, clamped to 512 MB - 8 GB). | Must |
| FR-IMG-13 | GPU texture memory shall be tracked separately from CPU memory, and GPU textures for off-screen tiles shall be released when GPU memory is limited. | Should |
| FR-IMG-14 | Tiles from closed slides shall be evicted from the cache immediately. | Must |

### 6.5 Color Management

| ID | Requirement | Priority |
|----|------------|----------|
| FR-IMG-15 | The application shall respect embedded ICC profiles for accurate color reproduction. | Should |
| FR-IMG-16 | Non-destructive brightness, contrast, and white balance adjustments shall be available per slide (display-only, never modifying the source). | Must |

---

## 7. Remote Access Requirements

### 7.1 Remote Slide Access

| ID | Requirement | Priority |
|----|------------|----------|
| FR-REM-01 | The application shall open slides from HTTP/HTTPS tile servers using a REST API. | Must |
| FR-REM-02 | Remote and local slides shall be interchangeable from the user's perspective (same ISlideSource interface). | Must |
| FR-REM-03 | Remote tiles shall be cached on local disk (default 10 GB, configurable) to avoid re-downloading across sessions. | Must |
| FR-REM-04 | HTTPS shall be enforced for all remote connections, with certificate validation via the system trust store. | Must |

### 7.2 Authentication

| ID | Requirement | Priority |
|----|------------|----------|
| FR-REM-05 | The application shall support HTTP Basic Auth, OAuth 2.0 bearer tokens, and client certificate authentication for remote tile servers. | Must |
| FR-REM-06 | Credentials shall be stored in the OS keychain (macOS Keychain, Windows Credential Manager, Linux Secret Service). | Must |

### 7.3 Connection Handling

| ID | Requirement | Priority |
|----|------------|----------|
| FR-REM-07 | On network interruption, cached tiles shall remain visible with a status banner ("Connection lost. Showing cached data. Reconnecting..."). | Must |
| FR-REM-08 | Auto-retry connection every 5 seconds on network loss. | Must |
| FR-REM-09 | Client-side request rate limiting (default 60 requests/second, configurable) shall prevent overwhelming shared tile servers. | Should |
| FR-REM-10 | Request coalescing shall cancel tile requests that are no longer visible due to rapid viewport changes. | Must |

---

## 8. Scripting Support

Scripting enables power users and researchers to automate workflows, perform batch operations, and run image analysis algorithms within the application. Scripting is planned for a later release (not v1).

### 8.1 Scripting Engine

| ID | Requirement | Priority |
|----|------------|----------|
| FR-SCR-01 | The application shall embed a Python interpreter for executing user scripts. | Must |
| FR-SCR-02 | The embedded Python interpreter shall run in a background thread and shall not block the UI thread. | Must |
| FR-SCR-03 | Scripts shall run to completion once started. The user shall be able to cancel a running script. | Must |
| FR-SCR-04 | The application shall provide an interactive script console (REPL) for entering and executing Python code. | Must |
| FR-SCR-05 | The application shall allow executing script files (.py) from disk via a menu action or the script console. | Must |
| FR-SCR-06 | Script execution results, print output, and errors shall be displayed in the script console. | Must |
| FR-SCR-07 | The application shall provide a built-in script editor with Python syntax highlighting. | Should |

### 8.2 Scripting API

| ID | Requirement | Priority |
|----|------------|----------|
| FR-SCR-10 | Scripts shall be able to query slide metadata (dimensions, resolution, pyramid levels, format, scanner info). | Must |
| FR-SCR-11 | Scripts shall be able to read pixel data from arbitrary regions and pyramid levels of the open slide. | Must |
| FR-SCR-12 | Scripts shall be able to create, modify, and delete annotations on the open slide. Annotation changes shall be reflected in the viewport in real time. | Must |
| FR-SCR-13 | Scripts shall be able to control viewport navigation (pan, zoom, go to coordinates, switch pyramid level). | Must |
| FR-SCR-14 | Scripts shall be able to open and close slides and iterate over slides in a case. | Must |
| FR-SCR-15 | Scripts shall be able to access measurement and calibration data (microns-per-pixel, scale). | Must |
| FR-SCR-16 | Scripts shall be able to perform batch operations across multiple slides (e.g., export annotations from all slides in a case). | Must |
| FR-SCR-17 | Scripts shall be able to write messages to the application log. | Must |
| FR-SCR-18 | Pixel data shall be accessible to scripts as NumPy arrays. | Must |
| FR-SCR-19 | Scripts shall be able to access and modify annotation properties (label, classification, color, layer, notes). | Must |
| FR-SCR-20 | Scripts shall be able to create and manage annotation layers. | Should |

### 8.3 Image Analysis

| ID | Requirement | Priority |
|----|------------|----------|
| FR-SCR-30 | The scripting API shall support image analysis workflows: read a region, process pixel data, create annotations from results. | Must |
| FR-SCR-31 | Scripts shall be able to report progress to the UI (progress bar or percentage) during long-running analysis tasks. | Should |
| FR-SCR-32 | Scripts shall be able to use third-party Python packages installed in the Python environment (e.g., NumPy, scikit-image, OpenCV, PyTorch). | Must |

### 8.4 Debugging

| ID | Requirement | Priority |
|----|------------|----------|
| FR-SCR-40 | The application shall support script debugging with breakpoints, step-over, step-into, and variable inspection. | Must |
| FR-SCR-41 | The user shall be able to set breakpoints in the built-in script editor. | Must |
| FR-SCR-42 | The debugger shall display the current call stack and local/global variable values. | Should |

### 8.5 Script Management

| ID | Requirement | Priority |
|----|------------|----------|
| FR-SCR-50 | Scripts shall be standard Python files (.py) that can be shared, copied, and version-controlled by users. | Must |
| FR-SCR-51 | The application shall maintain a configurable list of script directories from which scripts can be loaded. | Should |
| FR-SCR-52 | Recently executed scripts shall be listed for quick re-execution. | Should |

---

## 9. Non-Functional Requirements

### 9.1 Performance

| ID | Requirement | Target | Priority |
|----|------------|--------|----------|
| NFR-PERF-01 | Application launch time | < 3 seconds | Must |
| NFR-PERF-02 | Open slide (local) to first view | < 2 seconds | Must |
| NFR-PERF-03 | Open slide (remote) to first view | < 5 seconds | Must |
| NFR-PERF-04 | Pan to cached adjacent region | < 200 ms | Must |
| NFR-PERF-05 | Zoom one level to sharp view | < 300 ms | Must |
| NFR-PERF-06 | Switch slides in a case | < 1 second | Must |
| NFR-PERF-07 | Annotation creation response | < 100 ms | Must |
| NFR-PERF-08 | Pan/zoom frame rate | 60 fps on 2020+ hardware | Must |
| NFR-PERF-09 | Input latency (mouse/keyboard to display) | < 16 ms | Must |
| NFR-PERF-10 | Memory usage for single-slide viewing | < 4 GB | Must |
| NFR-PERF-11 | Memory usage for multi-slide comparison (4 slides) | < 8 GB | Should |

### 9.2 Reliability

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-REL-01 | Annotations shall never be lost due to application crash (auto-save + session recovery). | Must |
| NFR-REL-02 | The application shall be stable under extended use (8+ hour clinical sessions). | Must |
| NFR-REL-03 | Corrupt or incomplete slide files shall be handled gracefully (error message, not crash). Individual corrupt tiles shall display a hatched pattern. | Must |
| NFR-REL-04 | Session recovery on restart: open slides, viewport positions, and unsaved annotations restored from auto-save. | Must |

### 9.3 Scalability

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-SCALE-01 | The application shall handle cases with up to 200 slides without degraded UI performance, with virtual scrolling and sub-grouping (by block, stain, date) in the slide tray. | Must |
| NFR-SCALE-02 | The annotation system shall remain responsive with up to 10,000 annotations per slide. | Must |
| NFR-SCALE-03 | For annotation counts exceeding 5,000, incremental save (delta log with periodic compaction) should be used to maintain save performance. | Should |
| NFR-SCALE-04 | The slide tray panel should support virtual scrolling for cases with 100+ slides. | Should |

### 9.4 Cross-Platform Compatibility

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-CROSS-01 | The application shall behave consistently on Windows 10/11, macOS 12+, and Ubuntu 22.04+. | Must |
| NFR-CROSS-02 | The application shall use native file dialogs, system tray integration, and OS-standard keyboard shortcuts. | Must |
| NFR-CROSS-03 | The application shall support HiDPI displays with correct scaling on all platforms. | Must |
| NFR-CROSS-04 | The rendering layer shall be abstracted behind an `ITileRenderer` interface to support alternative backends (Metal, Vulkan, Qt RHI) when OpenGL is deprecated on a target platform. This is critical given Apple's OpenGL deprecation. | Must |
| NFR-CROSS-05 | All keyboard shortcuts shall be tested on all target platforms with US and at least one European keyboard layout. A shortcut conflict detector shall run at startup. All shortcuts shall be user-configurable. | Must |

### 9.5 Accessibility

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-ACC-01 | All non-image UI elements shall be accessible via screen readers. | Must |
| NFR-ACC-02 | All controls shall be reachable via keyboard (Tab/Shift+Tab navigation). | Must |
| NFR-ACC-03 | Minimum touch/click target: 32x32 logical pixels. | Must |
| NFR-ACC-04 | Adjustable font sizes in all UI panels. | Should |
| NFR-ACC-05 | High-contrast annotation mode (white outer stroke, colored inner stroke) for visibility against any tissue stain. | Must |

### 9.6 Maintainability

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-MAINT-01 | Clean separation between domain logic (pure C++), application services, infrastructure (I/O), and UI (Qt). | Must |
| NFR-MAINT-02 | The annotation file format shall be documented and versioned for forward compatibility. | Must |
| NFR-MAINT-03 | A plugin architecture shall provide extension points for AI analysis, custom annotation types, export formats, and institutional integrations. | Must |
| NFR-MAINT-04 | SlideIO shall be accessed through an adapter interface, isolating the application from SlideIO API changes. | Must |
| NFR-MAINT-05 | A compatibility test suite shall verify slide opening across all supported formats against reference files. | Should |

---

## 10. Security and Compliance Requirements

### 10.1 Patient Data Protection

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-SEC-01 | Slide label/macro images shall be hidden by default. | Must |
| NFR-SEC-02 | A presentation mode shall hide all patient-identifying information. | Must |
| NFR-SEC-03 | An "Export for sharing" function shall strip or redact patient-identifying metadata from annotations and case files before export. | Must |
| NFR-SEC-04 | The application shall support annotation file encryption at rest for institutional deployments. | Should |
| NFR-SEC-05 | The application shall not require real patient identifiers. Anonymized/pseudonymized IDs shall be supported for research. | Must |

### 10.2 Audit Trail

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-SEC-06 | Structured audit logging shall record: every slide open, annotation CRUD, export, and share action, with timestamp, user identity, and slide identifier. | Must |
| NFR-SEC-07 | Audit logs shall be stored in a separate, append-only file. | Must |
| NFR-SEC-08 | For institutional deployments, audit events should be forwardable to an external SIEM/audit system via syslog or webhook. | Should |

### 10.3 Application Security

| ID | Requirement | Priority |
|----|------------|----------|
| NFR-SEC-09 | No code from slide files shall be executed (protection against malicious file payloads). | Must |
| NFR-SEC-10 | HTTPS with certificate validation shall be required for remote connections. | Must |
| NFR-SEC-11 | Plugins shall require code signing or explicit user approval before loading. | Should |
| NFR-SEC-12 | A plugin allowlist shall be supported for institutional management. | Should |
| NFR-SEC-13 | Local annotation files shall not be executable. | Must |
| NFR-SEC-14 | AI analysis plugins and other long-running plugin computations shall execute in a separate process with IPC, not in the main viewer process. | Should |
| NFR-SEC-15 | Plugins that crash more than twice shall be auto-disabled. A "safe mode" startup shall disable all plugins. | Must |

---

## 11. Open Items and Future Scope

### 11.1 Deferred to v2

- **Real-time multi-user annotation** -- collaborative editing of annotations across network (requires annotation server architecture).
- **Cloud-native slide storage** -- direct S3/Azure Blob access as a slide source.
- **Python scripting support** -- embedded Python interpreter with scripting API, interactive console, debugger, and image analysis workflows (see Section 8).
- **AI integration framework** -- standardized interface for deep learning model inference, built on the scripting engine.
- **DICOM WSI first-class support** -- native DICOM query/retrieve, not just file-based access.
- **Regulatory compliance** -- FDA 510(k) / CE marking assessment if the viewer is used for primary diagnosis.
- **SQLite annotation backend** -- for annotation sets exceeding 10,000 per slide.
- **Vulkan/Metal rendering** -- for improved GPU memory control and macOS future-proofing.
- **Web companion viewer** -- browser-based lightweight viewer sharing the same tile server.

### 11.2 Decisions Required

1. **Annotation interoperability standard:** The native format is GeoJSON-based. Should we also adopt W3C Web Annotation as a secondary export format?
2. **Offline caching depth:** How many slides should be cacheable locally for offline clinical use?
3. **Plugin marketplace:** Should v1 include a plugin discovery/install mechanism, or is manual file placement sufficient?

---

*This SRS incorporates requirements from Phase 1 (Requirements Discovery), UX patterns from Phase 2, and refinements from Phase 5 (Critical Review). It serves as the primary requirements reference for development.*
