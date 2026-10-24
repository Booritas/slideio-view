# Measurement Calibration Override — Design

Stage 3 item 10. FR-MEAS-04 (manual override), FR-MEAS-05 (warning badge),
UI design §4.7 and §11.5, architecture `CalibrationManager`, critical review 1.4.

## 1. Why

Every physical number the viewer shows comes from one pair of values: the
scanner's pixel size in the slide metadata (`SlideInfo::resolutionX/Y`, in
metres). Today it reaches four places — the scale bar, the properties panel's
area and length, the measurements CSV and the GeoJSON export's provenance — and
the user has no way to correct it. Two failures follow:

- **Missing metadata**: the panel silently falls back to pixels, the scale bar
  has nothing to draw, and nothing says why. A pathologist who needs a tumour
  size from such a slide has no route to one.
- **Wrong metadata**: some scanners and converters write a wrong value (a
  missing objective factor, a 20× value on a 40× scan). Every micron figure is
  then wrong by a constant factor and looks exactly as trustworthy as a right
  one.

The critical review ranks this **High** (risk 6). This design lets the user set
the pixel size for a slide, and makes it visible everywhere a measurement is
shown or filed whether the number rests on the scanner's metadata, on a value
the user typed, or on nothing.

## 2. Goals, non-goals, anti-goals

**Goals**

- Set, change and clear a manual pixel size (µm per pixel, X and Y) for the
  open scene.
- One **calibration source** — `metadata`, `manual` or `none` — computed in one
  place and shown on every surface that shows a physical number: properties
  panel, scale bar, Slide Properties panel, both measurement-bearing exports.
- A badge on measurements whenever the source is not `metadata`.
- A one-line, non-modal prompt when a slide with no calibration opens.

**Non-goals**

- Calibrating from a reference object (draw a line over a stage micrometer and
  type its length). Useful, later; it needs the ruler tool first.
- Changing the display unit (µm/mm) — FR-MEAS-02 already exists as a
  preference.
- Any change to annotation coordinates. Annotations are stored in level-0
  pixels and stay there; a calibration changes only how they are measured.

**Anti-goals** — it fails even if it works when:

- A measurement on a manually calibrated slide can be read, exported or
  printed without saying so.
- Two surfaces disagree about the pixel size of the same scene.
- An override set for one slide applies to another, or to another scene of the
  same multi-scene file.
- Opening an uncalibrated slide blocks the user with a dialog.

## 3. Where the override lives — a per-scene preference, not the annotation file

The override is stored **in QSettings, keyed by (slide ID, scene index)**,
exactly as the per-slide ICC override is stored by slide ID
(`QSettingsColorProfileOverrideStore`, "Nothing is pruned … removal is
explicit").

The rejected alternative is a `calibration` member in the annotation file,
which UI design §11.5 suggests ("the annotation file records the calibration
source"). It is rejected for this round because:

1. **An older build drops it on rewrite.** The parser reads named top-level
   keys and the writer builds the root from `AnnotationDocument`, so a build
   from before this change would load the file, autosave it, and silently
   remove the override — the same trap that kept the counting category out of
   a separate field (CLAUDE.md, "Categorized counting uses `classification`").
   A silently reverted calibration is exactly the "wrong number that looks
   right" this design exists to remove.
2. **It would need an editable slide.** The annotation file is written only
   while the persistence service is active. A read-only scene — locked by
   another window, or opened after a refused file — is precisely where a reader
   most wants correct measurements, and could not be calibrated.
3. **The record §11.5 asks for is kept where the numbers are filed.** The
   annotation file holds level-0 pixel coordinates, which no calibration
   changes. The physical numbers leave the application through the
   measurements CSV and the GeoJSON export, and both now carry the source and
   the pixel size used (§6). That is the reproducibility the requirement is
   for.

The cost, stated: an override is per machine and per user profile. A colleague
opening the same slide on another workstation sees the metadata value (or
none) until they set it too. The badge makes that visible on their screen.

Key: `calibration/overrides/<slideId>.s<sceneIndex>`, holding `x` and `y` in
µm per pixel and `slideDisplayName` for the manage list (display only, never
matched against, as the ICC store does). **A scene that could not be
identified (empty slide ID) cannot carry an override**: the action is disabled
with a status tip saying why, because an override keyed on nothing would apply
to every unidentified slide.

## 4. The rule — `core::effectiveCalibration`

A pure core function decides, so every surface asks the same question:

```cpp
enum class CalibrationSource { Metadata, Manual, None };

struct Calibration
{
    PixelSize micronsPerPixel;   // {0, 0} when Source is None
    CalibrationSource source;
};

Calibration effectiveCalibration(double metadataXMetres, double metadataYMetres,
                                 std::optional<PixelSize> overrideMicrons);
```

- An override, when present and valid, **wins over metadata** — that is what
  "metadata is wrong" means.
- Metadata counts only when **both** axes are finite and positive; one good
  axis is not a calibration (pixels are not necessarily square, and a
  perimeter does not scale by one factor — the existing panel rule).
- Otherwise `None`.
- Valid override: both components finite and within **0.01 – 100 µm/px**. The
  bounds are a typo guard, not physics: real scans run about 0.1 – 2 µm/px,
  and a value outside the range is far likelier to be mm or nm typed as µm.
  The store refuses to save an invalid one; the function treats a stored
  invalid one (a hand-edited registry) as absent.

## 5. Setting it — Calibration dialog

**View > Calibration…**, beside "Manage Slide ICC Profiles…" — both are
per-slide corrections of what the scanner wrote. (The UI design's "Slide >
Calibration Override" assumed a Slide menu that does not exist; adding a menu
for one item is not worth it.) Enabled whenever a slide is open and
identified, **including read-only scenes** — it writes no annotation.

The dialog shows, for the open scene:

- **From the slide's metadata**: `0.2520 × 0.2520 µm/px`, or "none".
- **Use**: two numeric fields, X and Y, µm per pixel, 4 decimals, with a
  **Square pixels** check box (on by default, Y follows X). Pre-filled with the
  current override, else the metadata value, else empty.
- **Reset to metadata** — removes the override (disabled when there is none).
- OK applies; Cancel changes nothing. OK with a value outside the range is
  refused in the dialog with the range named.

Applying takes effect at once on every surface (§6) — no reopen — and is
logged at info level: slide ID, scene, old and new source and values (the
critical review's "log calibration source").

**Not on the undo stack**, for the same reason the category list is not: the
undo stack is per slide and cleared on a slide change, while this is a
preference. Reset is the way back.

## 6. Where it shows

| Surface | `Metadata` | `Manual` | `None` |
|---|---|---|---|
| Properties panel, Area/Length/Perimeter | value, no badge | value + amber badge **"manual calibration"** | value in px + amber badge **"uncalibrated"** |
| Scale bar (zoom indicator) | as today | as today + "(manual)" after the length | **"Uncalibrated"** + warning icon, no bar |
| Slide Properties panel | resolution as today | metadata row as today **plus** "Calibration: manual, X × Y µm/px" | "Calibration: none" |
| Measurements CSV | `calibration_source` = `metadata` | `manual`, and `pixel_width_um`/`pixel_height_um` are the **manual** values | `none`, micron columns empty (as today) |
| GeoJSON export, collection `slideio` block | `calibrationSource: "metadata"` | `"manual"`, `pixelSizeMicrons` the manual pair | `"none"`, no `pixelSizeMicrons` (as today) |

- The badge is text in the panel beside the measurement rows, with a tooltip
  naming the value and where it came from — text, not colour alone.
- The CSV gains **one column, `calibration_source`, placed after
  `pixel_height_um`**, beside the values it qualifies. The header stays fixed
  whatever the slide. This changes the header for existing readers of the file;
  it is the one place a downstream script will notice, and the right one.
- The counts export has no measurements and is unchanged.
- On opening a scene whose source is `None`, the status bar shows for a few
  seconds: "This slide has no calibration — measurements are in pixels. Set one
  under View > Calibration…". No dialog (anti-goal 4).

## 7. Code shape

- **core**: `Calibration.{h,cpp}` — `CalibrationSource`, `Calibration`,
  `effectiveCalibration`, `isValidCalibrationOverride`, the range constants,
  and `calibrationSourceName` (`"metadata" | "manual" | "none"`, shared by both
  exports). Interface `ICalibrationOverrideStore` (`find`, `set`, `remove`,
  `all`) with a `CalibrationOverride` struct (slide ID, scene, x, y, display
  name) — mirroring `IColorProfileOverrideStore`.
- **infra**: `QSettingsCalibrationOverrideStore`, INI-file constructor for
  tests, as the ICC store has.
- **ui**: `CalibrationDialog` (thin; its rules — prefill, square-pixel linking,
  validation message — in a pure `CalibrationDialogPolicy` a test can reach);
  `MainWindow` computes the effective calibration once on scene open and on
  apply, and hands the **same** `Calibration` to the zoom indicator, the
  properties panel, the Slide Properties panel and both export contexts —
  replacing the five places that read `info.resolutionX/Y` directly today.
  `MeasurementExportContext` and `GeoJsonExportContext` gain a
  `CalibrationSource`.
- The ruler tool, when it lands, reads the same `Calibration`.

## 8. Tests

- core: `effectiveCalibration` — metadata only; override wins over metadata;
  one-axis metadata is `None`; invalid stored override ignored; range bounds
  inclusive; NaN and infinity refused.
- infra: store round trip; key is per scene (scene 0 override does not apply
  to scene 1); remove; invalid values refused by `set`.
- ui: measurements CSV header has `calibration_source` after
  `pixel_height_um`; each source writes its name and the right pixel columns;
  GeoJSON block carries `calibrationSource` and omits `pixelSizeMicrons` for
  `None`; dialog policy prefill order and square-pixel linking; panel badge text
  per source (as a pure function).

## 9. Manual checks — group AK

Written before the run.

| | Check | Result |
|---|---|---|
| AK1 | A slide with metadata: panel, scale bar and Slide Properties show it with no badge; CSV `calibration_source` = `metadata`. | **Pass.** CMU-1-Small-Region: polygon 85600 µm², no badge; Slide Properties "metadata, 0.4990 × 0.4990 µm/px"; CSV `metadata`, 0.499. |
| AK2 | View > Calibration… shows the metadata value; set 0.5 × 0.5: the selected polygon's area changes by (0.5 / metadata)², the badge reads "manual calibration", the scale bar shows "(manual)". | **Pass.** 85600 → 85900 µm² (×1.004 = (0.5/0.499)²); badge "manual calibration"; scale bar "100 µm (manual)". |
| AK3 | Close and reopen the slide, and restart the viewer: the override is still applied. | **Pass** across a viewer restart (which closes and reopens the slide): 43000 µm², badge, on relaunch. |
| AK4 | A multi-scene file: an override on scene 0 does not change scene 1's numbers. | **Open — for the owner.** The override was set on scene 0 of the Leica SCN (2000 µm (manual) on its scale bar), but scene 1 could not be reached: on 2026-10-24 scripted clicks into the dock panels did not register, the condition recorded in group AJ. The per-scene key is pinned by `"an override is per scene"`. |
| AK5 | Reset to metadata: every surface is back to AK1. | **Pass.** Panel 85600 µm² with no badge, scale bar "100 µm", Slide Properties "metadata, 0.4990 × 0.4990 µm/px", the next CSV `metadata` 0.499. |
| AK6 | A slide with no calibration: status-bar prompt on open, "Uncalibrated" on the scale bar, "uncalibrated" badge, measurements in px; set an override and they turn into µm with the manual badge. | **Pass** on CMU-1-Small-Region-page-2.tif: the status-bar prompt on open, "Uncalibrated" with a warning icon, a rectangle 2975 px² / 225 px badged "uncalibrated"; at 0.5 it read 744 µm² / 113 µm badged "manual calibration". |
| AK7 | Square pixels off, X 0.25 Y 0.5: a rectangle's area and perimeter follow both axes. | **Pass.** The CMU polygon at 0.25 × 0.5: 43000 µm² and 904 µm, against 42953 µm² and 904.0 µm computed from its vertices. |
| AK8 | Enter 250 (mm typed as µm) and 0: refused in the dialog with the range named; nothing stored. | **Pass.** 250 and 0 each left the dialog open with "Enter a pixel size between 0.01 and 100 µm per pixel."; the stored value was unchanged. |
| AK9 | A read-only scene (second window): the dialog works and the numbers change; the annotation file is unchanged byte for byte. | **Pass.** A second window on CMU (Rectangle and Text disabled): set 0.3, the polygon read 30900 µm² (30926 computed); the annotation file hash unchanged. |
| AK10 | Export Measurements and GeoJSON with a manual override: the CSV and the GeoJSON carry `manual` and the manual pixel size. | **Pass.** CSV `0.25,0.5,manual` on every row; GeoJSON `calibrationSource: "manual"`, `pixelSizeMicrons: [0.25, 0.5]`. |
| AK11 | The log has one info line per apply and per reset, naming slide, scene and both sources. | **Pass.** One `Calibration for <slide> scene <n>: <old source> <x>x<y> -> <new source> <x>x<y>` line per apply and per reset, including none → manual and manual → none. |

Run 2026-10-24 against `28fc1f2`, **10 of 11 pass, AK4 open for the owner**; the settings had no calibration group before the run, and every override set was reset, so none is left stored. One behaviour observed outside the checks: a window measures with the override it last read, so after a second window changed it the first kept its old value until the scene was reopened, while its dialog already prefilled the new one. First recorded as acceptable; the whole-branch review showed the reverse case -- a window opened on the metadata stays unbadged after another window sets an override, and exports `metadata` -- so it was fixed: a window re-reads the store on activation, before each export and before the dialog (`01f9606`), checked by hand with two windows.

## 10. Left to the builder

- The exact badge styling and the warning icon (a standard Qt icon is fine).
- Whether the dialog also lists all stored overrides (a manage view like the
  ICC one). Not required; a later round can add it.
- The status-bar message's duration.
