# Open Slide from S3 — design

**Date:** 2026-05-30
**Status:** Approved (proceeding to implementation plan)

## Goal

Let the user open a whole-slide image hosted on AWS S3 by pasting a **presigned
URL** and selecting the slide driver explicitly. slideio reads remote slides
natively — `slideio::openSlide(path, driverId)` already accepts `http://`,
`https://`, and `s3://` URIs as the `path` argument (see the sibling
`slideio` repo: `imagetools/uridispatcher.cpp`, `imagetools/httpstream.cpp`,
where presigned-URL query strings and GET-signed S3 URLs are handled). So this
feature is a UI addition that funnels a URL + driver into the existing
`MainWindow::openSlide` path; no infrastructure (`infra`/`core`) changes.

## Non-goals

- **No auto-detect driver.** A presigned URL carries no usable file extension,
  so the driver combo offers only the named drivers — the user must choose one.
- **No URL persistence.** The dialog opens with an empty URL field every time.
  Presigned URLs embed temporary credentials and expire, so they are
  deliberately not remembered.
- **No Recent Files entry for remote URLs.** For the same credential/expiry
  reason, opening from S3 does not add the URL to the persistent Recent Files
  list.
- **No `s3://` translation in the viewer.** Whatever string the user pastes is
  passed verbatim to `openSlide`; slideio handles `https://` presigned URLs and
  `s3://` URIs itself.
- **No credential entry / SDK integration.** The app never holds AWS keys; the
  presigned URL is the entire authentication mechanism.

## Architecture

The viewer enforces strict downward layering: `core` is pure C++, `infra`
wraps SlideIO, `ui` consumes both and adds widgets. This feature lives entirely
in `ui` and reuses existing entry points.

```
OpenS3Dialog (new, ui)
   │  user enters presigned URL + picks a driver, clicks "Open Slide"
   ▼
MainWindow openFromS3Action handler
   │  dlg.exec() == Accepted → openSlide(dlg.presignedUrl(), dlg.driverId())
   ▼
MainWindow::openSlide(path, driverId)   (existing; one guard added)
   │  skip addToRecentFiles() for http/https/s3 paths
   ▼
ViewportWidget::openSlide → SlideIOAdapter → slideio::openSlide(url, driverId)
```

## Components

### `OpenS3Dialog` (new — `src/ui`)

- Files: `src/ui/include/slideio/viewer/ui/OpenS3Dialog.h`,
  `src/ui/src/OpenS3Dialog.cpp`. Added to `src/ui/CMakeLists.txt`.
- A `QDialog` subclass following the repo's PIMPL convention for Qt widgets.
- Layout (form):
  - `QLineEdit` — "Presigned URL". Placeholder text shows an example
    `https://…` URL.
  - `QComboBox` — "Driver".
  - `QDialogButtonBox` with an **Open Slide** button (`AcceptRole`, set as
    default) and a **Cancel** button (`RejectRole`).
- **Driver combo population:** reuse `availableDriverFilters()` and keep only
  entries with a non-empty `driverId` (i.e., the named per-driver entries such
  as "Aperio SVS" → `SVS`; the "All Supported"/"All Files" auto entries are
  dropped). Each item stores its `driverId` (`QString`) as item data and shows
  the driver's `displayName` as text.
- **Validation:** the Open Slide button is disabled whenever the trimmed URL
  field is empty, so a load is never started without a URL. Connected to the
  line edit's `textChanged` signal.
- **Accessors** (read after `exec()` returns `QDialog::Accepted`):
  - `std::string presignedUrl() const` — the trimmed URL text.
  - `std::string driverId() const` — the selected combo item's stored driver id.

### `MainWindow` changes

- New `QAction* openFromS3Action`, text "Open Slide from &S3...",
  status tip "Open a whole-slide image from an S3 presigned URL". Created in
  `Impl::createActions()`.
- Added to the File menu in `Impl::createMenus()` immediately after
  `openAction` (before the Recent Files submenu).
- Triggered handler (in `Impl::connectSignals()`): construct an `OpenS3Dialog`
  parented to `owner`, call `exec()`; on `Accepted`, call
  `owner->openSlide(dlg.presignedUrl(), dlg.driverId())`.
- **`openSlide` guard:** before calling `addToRecentFiles(path)`, skip it when
  `path` begins (case-insensitive) with `http://`, `https://`, or `s3://`. The
  window-title update is unchanged. A small local helper
  (e.g. `isRemoteSlidePath`) expresses the check.

## Error handling

Unchanged. A bad, expired, or wrong-driver URL causes the open to fail in the
background; the existing `ViewportWidget::errorOccurred` →
`QMessageBox::critical(owner, "Error", …)` connection surfaces the message.

## Testing

`OpenS3Dialog` is thin UI glue with no domain logic, matching the other
dialog/panel classes in `src/ui` (e.g. `AssociatedImageWindow`,
`SlidePropertiesPanel`), none of which carry unit tests. The driver-list reuse
is already exercised by the existing `DriverFilters` code path. No new unit
test is added. Verification is a manual build + smoke test: the menu item opens
the dialog, the combo lists drivers, Open Slide is disabled on an empty URL,
and a valid presigned URL loads the slide.
