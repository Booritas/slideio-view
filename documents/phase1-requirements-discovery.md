# Phase 1: Requirements Discovery

**Author:** Pathologist (Domain Expert)
**Date:** 2026-03-13
**Status:** Complete

---

## 1. Clinical Use Cases

### 1.1 Primary Diagnosis

The core use case: a pathologist receives digitized tissue slides and renders a diagnostic opinion.

- **Workflow:** Open a case containing 1–20+ slides (H&E and special stains), systematically examine tissue at multiple magnifications (typically scanning at 4x–10x, diagnosing at 20x–40x), annotate regions of interest, and produce a diagnostic report.
- **Critical requirements:** Sub-second slide load, smooth pan/zoom with no perceptible lag, accurate color reproduction, ability to quickly switch between slides in the same case.
- **Volume:** A busy surgical pathologist reviews 40–80 slides per day. Any friction in navigation or loading directly impacts throughput and diagnostic accuracy.

### 1.2 Second Opinion / Consultation

A pathologist shares a case with a colleague for expert consultation.

- **Workflow:** The consulting pathologist opens shared slides, reviews the referring pathologist's annotations and preliminary findings, adds their own annotations, and communicates their opinion.
- **Requirements:** Annotation sharing (view annotations from other users), ability to toggle annotation visibility by author, bookmark/snapshot sharing so consultants can navigate directly to regions of concern.

### 1.3 Tumor Board Review

Multi-disciplinary team (pathologist, oncologist, surgeon, radiologist) reviews cases together, typically projected on a large display.

- **Workflow:** Pathologist drives navigation while the team discusses. Must show specific regions, switch between slides, and present annotations clearly.
- **Requirements:** Presentation-friendly UI (minimal clutter, large controls), side-by-side slide comparison (e.g., H&E next to immunohistochemistry), full-screen mode, scalable UI for large/projected displays.

### 1.4 Teaching and Training

Faculty demonstrates findings to residents and students; trainees practice slide review.

- **Workflow:** Instructor pre-annotates teaching slides with labels and arrows, students navigate independently and create their own annotations for grading.
- **Requirements:** Read-only annotation layers (instructor annotations that students cannot modify), separate student annotation layers, ability to hide/reveal annotations as a teaching exercise, quiz/self-assessment mode where annotations can be hidden and then revealed.

### 1.5 Research

Researchers analyze tissue for quantitative studies — measuring structures, counting cells, comparing staining patterns across cohorts.

- **Workflow:** Open slides from a study cohort, create precise measurement annotations (distance, area, cell counts), export annotation data for statistical analysis, compare staining intensity across slides.
- **Requirements:** High-precision measurement tools, annotation export (coordinates, measurements, classifications) in standard formats (GeoJSON, CSV, or similar), batch slide opening, consistent coordinate systems for reproducibility.

### 1.6 Quality Assurance

Lab managers review scanned slides to verify scan quality, check for artifacts, and ensure staining consistency.

- **Workflow:** Rapidly scan through recently digitized slides, flag quality issues (focus problems, tissue folds, staining artifacts), log issues.
- **Requirements:** Fast slide browsing/thumbnailing, ability to flag slides with quality categories, access to scanner metadata (scan date, resolution, focus quality metrics).

---

## 2. User Personas

### 2.1 Clinical Pathologist

- **Profile:** Board-certified physician, 30–65 years old, high caseload pressure, variable computer literacy.
- **Goals:** Accurate diagnosis with minimal time per case, efficient navigation, reliable annotations.
- **Pain points:** Slow load times break concentration, unfamiliar UI patterns cause errors, too many clicks to accomplish routine tasks.
- **Usage pattern:** 6–10 hours/day, repetitive workflows, values keyboard shortcuts and muscle memory.
- **Environment:** Dual-monitor workstation in a hospital, one screen for the viewer, one for the LIS/reporting system.

### 2.2 Lab Technician / Histotechnologist

- **Profile:** Operates scanners, manages slide storage, performs quality checks.
- **Goals:** Verify scan quality, organize slides into cases, manage file storage.
- **Pain points:** Needs to open slides quickly without full diagnostic tools, wants a lightweight browsing mode.
- **Usage pattern:** Intermittent use throughout the day, brief interactions per slide.

### 2.3 Research Scientist

- **Profile:** PhD-level researcher, comfortable with software, needs quantitative tools.
- **Goals:** Precise measurements, reproducible annotations, data export for analysis pipelines.
- **Pain points:** Imprecise measurement tools, inability to export annotation data programmatically, lack of batch processing support.
- **Usage pattern:** Extended sessions analyzing specific tissue features across multiple slides.

### 2.4 Pathology Trainee / Student

- **Profile:** Medical student or pathology resident, learning to identify tissue patterns.
- **Goals:** Practice slide review, learn from annotated teaching sets, build diagnostic skills.
- **Pain points:** Overwhelmed by complex interfaces, needs guided navigation, benefits from annotation reveal/hide for self-testing.
- **Usage pattern:** Study sessions of 1–3 hours, often working through curated slide sets.

---

## 3. Typical Workflows

### 3.1 Slide Review Workflow (Primary Diagnosis)

1. **Case assignment:** Pathologist receives notification of a new case (external to viewer, from LIS).
2. **Case opening:** Opens the case, which contains 1–N slides. The viewer displays a case panel listing all slides with thumbnails.
3. **Slide selection:** Clicks the first slide; it loads in the main viewport at a low magnification overview.
4. **Systematic scanning:** Pans across the tissue at 4x–10x equivalent, looking for areas of concern. Uses the thumbnail/overview map to maintain orientation.
5. **High-power examination:** Zooms into areas of interest at 20x–40x to evaluate cellular detail. Smooth, continuous zoom is essential — discrete zoom steps are disorienting.
6. **Annotation:** Marks regions of interest with freehand outlines or rectangles, adds text labels (e.g., "invasive carcinoma"), places measurement rulers if needed.
7. **Slide switching:** Moves to the next slide in the case (special stains, deeper levels). Expects the viewer to remember the position on the previous slide.
8. **Comparison:** May view two slides side-by-side (e.g., H&E next to Ki-67 immunostain) to correlate findings.
9. **Report generation:** Finalizes annotations, exits to the reporting system. Annotations must persist.

### 3.2 Case Assembly Workflow

1. **Slide import:** Technician or pathologist imports scanned slide files from a local directory or network share.
2. **Case creation:** Groups slides into a logical case (patient + accession number).
3. **Metadata entry:** Associates patient identifier, stain type, tissue type, and block/slide identifiers with each slide.
4. **Quality check:** Briefly opens each slide to verify scan quality.
5. **Case save:** Saves the case definition for later review.

### 3.3 Annotation Workflow

1. **Tool selection:** Selects an annotation tool from the toolbar (rectangle, ellipse, freehand, point marker, ruler, area measurement, text label).
2. **Drawing:** Draws the annotation on the slide at the current magnification. The annotation must be resolution-independent — it is defined in slide coordinates, not screen coordinates.
3. **Properties:** Sets annotation properties: color, label text, classification category (e.g., "tumor", "necrosis", "normal"), line thickness.
4. **Editing:** Selects and modifies existing annotations — move, resize, reshape, change properties, delete.
5. **Layer management:** Annotations are organized into layers (e.g., "Diagnostic", "Teaching", "Research"). Layers can be shown, hidden, locked, or filtered by classification.
6. **Persistence:** Annotations auto-save and can be exported/imported independently of the slide file.

### 3.4 Reporting Workflow

1. **Snapshot capture:** Captures a screenshot of the current viewport with annotations visible, at a specific magnification, for inclusion in a report.
2. **Export:** Exports snapshots as PNG/JPEG images with embedded scale bar.
3. **Annotation summary:** Generates a text summary of all annotations (labels, measurements, classifications) for pasting into a report.

---

## 4. Annotation Requirements

### 4.1 Geometric Annotation Types

| Type | Description | Use Case |
|------|-------------|----------|
| **Rectangle** | Axis-aligned bounding box | Quick region selection, area of interest marking |
| **Ellipse** | Axis-aligned or rotatable ellipse | Marking rounded structures (glomeruli, follicles) |
| **Freehand polygon** | Closed, user-drawn polygon | Outlining irregular tumor boundaries, tissue regions |
| **Freehand line** | Open path, user-drawn | Tracing structures, measuring irregular paths |
| **Point marker / Pin** | Single coordinate with icon | Marking specific cells, mitotic figures, features |
| **Arrow** | Directed line with arrowhead | Pointing to specific features for teaching/presentation |
| **Ruler (distance)** | Line segment with length display | Measuring tumor depth, structure sizes |
| **Area measurement** | Closed polygon with area display | Measuring tumor area, lesion extent |
| **Angle measurement** | Two-line-segment angle | Measuring angular relationships (rare but used in orthopedic pathology) |
| **Text label** | Floating text box | Free-form notes, diagnostic labels |

### 4.2 Annotation Properties

- **Color:** User-selectable, with sensible defaults per classification category.
- **Line thickness:** Adjustable (1–5 px at screen resolution, scaling appropriately with zoom).
- **Opacity/fill:** Annotations can have transparent fills for region highlighting.
- **Label text:** Short text string attached to any geometric annotation.
- **Classification:** Categorical label from a configurable taxonomy (e.g., "Tumor", "Stroma", "Necrosis", "Normal", "Artifact"). Supports user-defined categories.
- **Author:** Automatically recorded (user identity).
- **Timestamp:** Creation and last-modified times.
- **Confidence level:** Optional field for research contexts (e.g., "certain", "probable", "possible").

### 4.3 Annotation Interaction

- **Selection:** Click to select; multi-select via Shift+click or rubber-band selection.
- **Move:** Drag selected annotations to reposition.
- **Resize/reshape:** Drag handles on rectangles/ellipses; drag vertices on polygons.
- **Vertex editing:** Add/remove vertices on freehand polygons for refinement.
- **Undo/redo:** Full undo/redo stack for annotation operations (minimum 50 levels).
- **Copy/paste:** Duplicate annotations within a slide or across slides.
- **Snapping:** Optional snap-to-grid or snap-to-annotation for alignment.
- **Z-order:** Annotations can be brought forward/sent backward when overlapping.

### 4.4 Annotation Storage

- **Format:** Annotations stored separately from slide images (slides are read-only files).
- **Serialization:** JSON-based format recommended for portability. Must store: geometry in slide coordinates (microns or pixels at level 0), properties, metadata.
- **Interoperability:** Consider compatibility with existing annotation standards (e.g., ASAP XML, QuPath GeoJSON, OMERO ROIs) for import/export.
- **Versioning:** Annotation files should support version tracking for collaborative use.

### 4.5 Measurement Calibration

- **Scale source:** Measurements derive scale from the slide's pixel-per-micrometer metadata (provided by SlideIO from the scanner).
- **Display units:** Micrometers (um) for histology-scale measurements; millimeters (mm) for gross/macro measurements. User-configurable.
- **Accuracy:** Measurements must be accurate to the resolution of the scanned image (typically 0.25 um/pixel at 40x).
- **Scale bar:** Always-visible or togglable scale bar on the viewport showing current physical scale.

---

## 5. Navigation Requirements

### 5.1 Pan and Zoom

- **Continuous zoom:** Smooth, continuous zoom from the lowest resolution (entire slide in view, typically 0.5x–1x) to the highest resolution (full scanner resolution, typically 40x or 60x).
- **Zoom methods:** Mouse scroll wheel, pinch gesture (trackpad/touch), keyboard shortcuts (+/-), double-click to zoom in at point, zoom slider in UI.
- **Zoom centering:** Zoom always centers on the cursor position (or viewport center for keyboard zoom).
- **Pan methods:** Click-and-drag (middle mouse button or left button when not in annotation mode), arrow keys, edge-of-screen panning (optional).
- **Inertial scrolling:** Optional momentum-based panning for a fluid feel (configurable, as some users find it disorienting).
- **Magnification display:** Current magnification always visible (e.g., "20x", "0.5 um/px").

### 5.2 Thumbnail / Overview Map

- **Always-visible minimap:** A small thumbnail of the entire slide displayed in a corner (configurable position), showing the current viewport extent as a rectangle.
- **Interactive:** Clicking/dragging on the minimap navigates the main viewport.
- **Annotation overlay:** Option to show annotation locations on the minimap as colored dots/regions.

### 5.3 Bookmarks and Navigation History

- **Bookmarks:** User can save the current viewport state (position, zoom level, active annotations) as a named bookmark. Bookmarks persist with the case/slide.
- **Navigation history:** Back/forward buttons (like a web browser) to revisit recently viewed positions.
- **Quick-jump:** Right-click context menu or keyboard shortcut to jump to specific magnifications (e.g., 1x, 5x, 10x, 20x, 40x).

### 5.4 Multi-Slide Comparison

- **Side-by-side view:** Split the viewport into 2–4 panels, each showing a different slide (or different region of the same slide).
- **Synchronized navigation:** Option to lock panels so panning/zooming one panel moves all panels correspondingly. Essential for comparing serial sections or matched stains.
- **Independent navigation:** Each panel can also navigate independently.
- **Overlay mode:** For advanced use: semi-transparent overlay of two aligned slides (e.g., H&E and IHC of adjacent serial sections), with opacity slider.

### 5.5 Slide Tray / Case Panel

- **Slide list:** Panel showing all slides in the current case as labeled thumbnails.
- **Sorting/grouping:** Sort by stain type, block number, scan date. Group by tissue block.
- **Quick preview:** Hovering over a slide thumbnail shows a larger preview tooltip.
- **Drag to viewport:** Drag a slide from the tray to a comparison panel.

---

## 6. Image Handling Requirements

### 6.1 Supported Formats

The application uses **SlideIO** as its image backend. SlideIO supports a wide range of whole-slide image formats including:

- **Aperio SVS** (.svs) — Leica/Aperio scanners, very common in clinical labs
- **Hamamatsu NDPI** (.ndpi) — Hamamatsu NanoZoomer scanners
- **Leica SCN** (.scn) — Leica biosystems
- **Ventana BIF** (.bif) — Roche/Ventana scanners
- **3D Histech MRXS** (.mrxs) — 3DHISTECH Pannoramic scanners
- **Philips iSyntax** (.isyntax) — Philips IntelliSite scanners
- **Generic TIFF** (.tif/.tiff) — including BigTIFF, pyramidal TIFF
- **DICOM WSI** — DICOM standard for whole-slide images (emerging standard)
- **Zeiss CZI** (.czi) — fluorescence and brightfield
- **OME-TIFF** (.ome.tiff) — open standard for microscopy

The viewer must not be format-aware at the application level; SlideIO abstracts format differences.

### 6.2 Image Pyramid Navigation

Whole-slide images are stored as multi-resolution pyramids (typically 3–8 levels). The viewer must:

- **Request tiles at the appropriate resolution level** based on current zoom.
- **Prefetch neighboring tiles** for smooth panning.
- **Decode tiles asynchronously** on background threads to avoid blocking the UI.
- **Cache decoded tiles** in memory with an LRU eviction policy.
- **Display lower-resolution tiles** as placeholders while higher-resolution tiles load (progressive refinement).

### 6.3 Image Characteristics

- **Typical file sizes:** 500 MB to 5 GB per slide; some fluorescence images exceed 10 GB.
- **Pixel dimensions:** 50,000 x 50,000 to 200,000 x 100,000 pixels at highest resolution.
- **Color depth:** 8-bit RGB for brightfield; 16-bit per channel for fluorescence.
- **Number of channels:** 1 (grayscale), 3 (RGB), or N (multi-channel fluorescence, up to 40+ channels).
- **Z-stacks:** Some scanners produce multi-focal-plane images; the viewer should support Z-level selection.

### 6.4 Local and Remote Storage

- **Local files:** Open slides from local disk or mounted network shares. Must handle file paths on Windows, Linux, and macOS.
- **Remote access:** Open slides from HTTP/HTTPS servers (e.g., institutional image servers). SlideIO may support streaming tile access for some formats.
- **Cloud storage:** Future consideration — access slides from S3-compatible or Azure Blob storage.
- **Performance:** Local files should open within 1–2 seconds. Remote files should show the first tile within 3–5 seconds, with progressive loading of additional tiles.

### 6.5 Color Management

- **ICC profiles:** Respect embedded ICC profiles in slide files for accurate color reproduction.
- **Display calibration:** Support system-level display calibration (OS color management).
- **White balance / brightness / contrast:** User-adjustable display settings per slide (non-destructive, display-only).
- **Fluorescence pseudo-coloring:** For multi-channel fluorescence, allow assignment of display colors to each channel, with channel-level brightness/contrast.

---

## 7. Metadata Requirements

### 7.1 Scanner Metadata (from slide file)

Extracted automatically via SlideIO:

- **Scanner manufacturer and model**
- **Scan date and time**
- **Scan resolution** (pixels per micrometer / objective magnification)
- **Image dimensions** (pixels and physical size in mm)
- **Number of focal planes** (Z-layers)
- **Compression method**
- **Focus quality metrics** (if available)

### 7.2 Slide-Level Metadata (user-entered or imported)

- **Slide identifier** (label barcode text)
- **Stain type** (H&E, PAS, IHC antibody name, special stain name)
- **Tissue type** (e.g., "liver biopsy", "skin excision", "lymph node")
- **Block identifier** (which tissue block the section comes from)
- **Section number** (for serial sections)
- **Preparation date**

### 7.3 Case-Level Metadata

- **Patient identifier** (MRN, anonymized ID, or study subject ID)
- **Accession number** (lab case identifier)
- **Requesting physician**
- **Clinical history** (brief free text)
- **Specimen type and site** (e.g., "colon, right hemicolectomy")
- **Date of procedure**

### 7.4 Privacy and Compliance

- **De-identification:** The viewer must not require real patient identifiers. It should support anonymized/pseudonymized IDs for research.
- **Label image:** Some slide formats embed a macro/label image showing the physical slide label (which may contain patient info). The viewer should provide an option to hide label images.
- **Audit trail:** For clinical deployments, log who accessed which slides and when (can be a simple local log file; full audit is typically handled by the LIS).

---

## 8. Usability Requirements for Clinical Environments

### 8.1 Performance Expectations

| Operation | Target Time |
|-----------|-------------|
| Application launch | < 3 seconds |
| Open a slide (local) | < 2 seconds to first view |
| Open a slide (remote) | < 5 seconds to first view |
| Pan to adjacent region | < 200 ms (tile already cached) |
| Zoom one level | < 300 ms to sharp view |
| Switch slides in a case | < 1 second |
| Create an annotation | Immediate (< 100 ms response) |

### 8.2 Minimal Clicks Principle

Clinical pathologists are under time pressure. Every additional click or dialog box costs time across thousands of slides per year. Design principles:

- **One-click slide open** from the case panel.
- **No modal dialogs** during routine workflows (annotation creation, navigation).
- **Toolbar always visible** — annotation tools accessible without menu diving.
- **Right-click context menus** for common actions on annotations and slide regions.
- **Keyboard shortcuts** for all frequent operations (zoom levels, annotation tools, slide switching, undo/redo).

### 8.3 Familiar UI Patterns

- **Map-like navigation** — pan/zoom behavior should mirror Google Maps or similar applications that pathologists already use.
- **Standard keyboard shortcuts** — Ctrl+Z/Cmd+Z for undo, Ctrl+S/Cmd+S for save, etc.
- **Consistent with desktop OS conventions** — native window chrome, standard menus (File, Edit, View, Tools, Help), drag-and-drop.
- **Dark/light theme** — some pathologists work in dimmed environments (tumor board rooms); offer both themes.

### 8.4 Error Recovery

- **Auto-save:** Annotations auto-save every 30 seconds and on every annotation completion.
- **Crash recovery:** On unexpected termination, restore the last session state (open slides, viewport positions, unsaved annotations).
- **Graceful degradation:** If a remote slide is unreachable, show cached tiles and display a clear status message rather than crashing.

### 8.5 Accessibility

- **High-contrast mode** for annotation outlines (configurable colors against various tissue stains).
- **Adjustable font sizes** in all UI panels.
- **Screen reader compatibility** for non-image UI elements (panels, menus, dialogs).
- **Colorblind-friendly default annotation palette** (avoid red/green only; use blue/orange/yellow).

### 8.6 Multi-Monitor Support

- **Detachable panels:** The slide tray, annotation list, and metadata panels should be detachable and movable to a secondary monitor.
- **Full-screen viewport:** The main slide viewport can fill one monitor entirely while tools and panels live on the other.
- **Saved layouts:** Users can save and recall window layouts.

---

## 9. Non-Functional Requirements Summary

### 9.1 Performance

- Smooth 60 fps panning and zooming on hardware from 2020 onwards.
- GPU-accelerated tile rendering where available (OpenGL or Vulkan via Qt).
- Memory usage under 4 GB for typical single-slide viewing; graceful handling up to 8 GB for multi-slide comparison.

### 9.2 Reliability

- No data loss: annotations must never be lost due to application crash.
- Stable under extended use (8+ hour clinical sessions).
- Handles corrupt or incomplete slide files without crashing (display error message, skip unreadable tiles).

### 9.3 Cross-Platform

- Windows 10/11, macOS 12+, Ubuntu 22.04+ / major Linux distributions.
- Consistent behavior and appearance across platforms (using Qt's cross-platform capabilities).
- Native look-and-feel integration where possible (file dialogs, system tray, notifications).

### 9.4 Maintainability

- Clean separation between image handling (SlideIO), UI (Qt), and application logic.
- Annotation format documented and versioned for forward compatibility.
- Plugin architecture for future extensions (AI-assisted analysis, additional annotation types, institutional integrations).

### 9.5 Security

- No execution of code from slide files (some formats may embed scripts).
- Secure handling of remote connections (TLS for HTTPS image servers).
- Local annotation files should not be executable.

---

## 10. Open Questions for Team Discussion

1. **Annotation interoperability:** Should we define our own annotation format or adopt an existing standard (QuPath GeoJSON, ASAP XML, W3C Web Annotation)?
2. **AI integration:** Should the architecture plan for future AI-assisted annotation (e.g., tumor detection overlays from deep learning models)?
3. **Real-time collaboration:** Is real-time multi-user annotation (like Google Docs for slides) a requirement, or is file-based sharing sufficient?
4. **DICOM integration:** Should the viewer support DICOM WSI natively as a first-class storage mechanism, or treat it as just another format via SlideIO?
5. **Offline mode:** For clinical deployments, should the viewer cache slides locally for offline use when the network is unavailable?
6. **Regulatory considerations:** Does the viewer need to comply with any medical device regulations (FDA 510(k), CE marking) if used for primary diagnosis?

---

*This document serves as input for Phase 2 (UX Concept) and ultimately feeds into Document 1 (Software Requirements Specification).*
