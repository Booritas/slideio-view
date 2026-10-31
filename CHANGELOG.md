# Changelog

All notable changes to SlideIO Viewer are recorded here, newest first. The
format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project uses [Semantic Versioning](https://semver.org/).

Add to **Unreleased** as changes land. At release time, rename it to the
version and date (`## [0.3.0] - YYYY-MM-DD`) and start a new empty Unreleased
section. The release workflow copies the version's section into the GitHub
Release and refuses to publish a version that has none.

## [Unreleased]

## [0.2.0] - 2026-10-31

### Added

- **Annotations.** Rectangle, ellipse, polygon, freehand line, point marker,
  arrow, text, ruler and angle tools, with select, move, delete, resize handles
  for boxes and vertex editing for traced shapes (double-click a polygon or
  line).
- Annotations are saved automatically, one file per slide and scene in a
  workspace folder chosen in Preferences, and come back when the slide is
  reopened. Changing the folder asks whether to copy the annotations on screen
  into it, merge them with what is already there, or switch without them.
- Undo and redo for annotation edits (Ctrl+Z / Ctrl+Y), 50 steps per slide.
- Annotation list and properties panels: find, navigate to, rename, classify,
  recolour and annotate any annotation.
- Measurements in the properties panel: area, perimeter, length and angle, in
  microns when the slide is calibrated.
- **Calibration** (View > Calibration...): override a slide's pixel size per
  scene. Every measurement, the scale bar and the exports show whether the
  numbers come from the scanner, a manual calibration, or neither.
- **Categorized counting**: a configurable category list, a counting category
  stamped onto new point markers, a live tally per category, and renaming a
  category across a slide.
- **Annotation layers**: per-scene layers with visibility, lock, opacity and
  order; move annotations between layers and merge layers. Counts include
  hidden layers.
- File > Export Counts: the category tally as CSV.
- File > Export Measurements: every annotation's measurements as CSV, in pixels
  and microns.
- File > Export Annotations as GeoJSON, readable by QuPath.
- File > Import Annotations: GeoJSON (including QuPath exports) and ASAP XML.
  Re-importing the same file adds nothing, and an import is one undo step.
- An annotation file is locked while a slide is open, so a second window
  opens it read-only instead of overwriting its changes.
- Help > SlideIO Viewer Help opens the online documentation.
- Help > License shows the licence and the third-party software the viewer
  ships with.
- Slides sent from macOS Finder open in the running viewer.

### Changed

- RGB brightfield slides open at their native intensity range.
- Turning on a pane brings it to the front.
- Channels are numbered from 1 in the Metadata pane, as in the channel mixer.
- A slide is added to Recent Files only once it has opened, and only once
  however its path is spelled.

### Fixed

- Escape leaves full screen, and the menus stay usable in full screen.
- Min/Max work on one-channel slides.
- The Z/T overlay is hidden when a slide closes.
- The window title no longer names a slide after it is closed.
- NDPI slides whose levels are stored in strips rather than tiles open faster.

## [0.1.2] - 2026-10-11

### Changed

- macOS packages run on macOS 12 and newer.

## [0.1.1] - 2026-10-09

### Added

- Colour management: show slides through their embedded ICC profile, a default
  profile for slides that have none, or a per-slide profile override, with a
  dialog to review and remove stored overrides.
- The slide's ICC profile is shown in the properties panel.
- An Intel macOS build.

### Fixed

- The file dialog and the documentation no longer list formats the library
  does not read.

## [0.1.0] - 2026-10-03

First release.

### Added

- Open and navigate whole-slide images through the SlideIO library, rendered
  on OpenGL from a cached tile pyramid.
- Scene thumbnails, associated images, slide properties and a metadata tree.
- Channel mixer with per-channel histograms, display range and log scale.
- Z-stack and time-series navigation.
- Minimap, zoom indicator and scale bar.
- DICOM studies opened from a directory.
- Windows installer, macOS disk images and a Debian package.

[Unreleased]: https://github.com/Booritas/slideio-view/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/Booritas/slideio-view/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/Booritas/slideio-view/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/Booritas/slideio-view/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Booritas/slideio-view/releases/tag/v0.1.0
