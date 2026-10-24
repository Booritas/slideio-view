# Annotation Layers — Design

The last open Stage 3 item: the `AnnotationLayer` of item 1 and the
`LayerPanel` of item 6. FR-ANN-23 (named layers with visibility, lock and
opacity, Must), FR-ANN-24 (default Diagnostic / Teaching / Research; create,
rename, delete, reorder, Must), FR-ANN-20's "layer assignment" in the context
menu; UI design §4.6 and the Layers tab of §3; the architecture's
`LayerManager` and `layers` / `layerId` in the annotation schema.

## 1. Why

A slide carries more than one kind of annotation. The diagnostic marks a
pathologist relies on; teaching marks an instructor wants students not to see
until after the exercise (SRS teaching workflow, steps 2–4); research marks
that should not clutter a sign-out. Today they are one undifferentiated list:
the only way to hide the answers is to delete them, and the only way to keep
an exploratory trace out of the way is to remember not to touch it.

Layers give each kind a name, a switch to hide it, a lock to protect it and an
opacity to fade it — and say, for every annotation, which kind it is.

## 2. Goals, non-goals, anti-goals

**Goals**

- Every annotation belongs to exactly one layer of its scene.
- Each scene's layers have a name, visibility, lock and opacity, and an order.
- New annotations go to the **active layer**; the status bar names it.
- A **Layers** tab: create, rename, delete, merge, reorder, show/hide, lock,
  set opacity, choose the active layer.
- **Move to Layer** for the selected annotation, from the viewport's and the
  list's context menus.
- Saved with the scene's annotations, in the same file.

**Non-goals**

- Filtering the list by author (FR-ANN-25) — a separate, list-panel change.
- Bookmarks' "active layers" (FR-NAV-02) — bookmarks are not built.
- Multi-select batch "move to layer" (no multi-select yet).
- Layers that span scenes or slides: a layer set is per scene, like the
  annotations it holds.
- A layer column in the measurements CSV — a fourth header change in a row is
  not worth it for this round; the GeoJSON export carries the layer (§7).

**Anti-goals** — it fails even if it works when:

- **An older build silently flattens or drops layer assignments.** A file whose
  layers mean something must be refused by a build that cannot keep them —
  never rewritten without them (the counting-category trap, CLAUDE.md).
- **A clinical count changes when a layer is hidden.** Visibility is a view,
  like the list's filter text; the tally and both exports count every layer.
- **A locked annotation changes**: no move, resize, vertex edit, property edit,
  delete or re-layer reaches an annotation on a locked layer.
- **An annotation is ever on no layer**, or on a layer the scene does not have.

## 3. The model

```cpp
struct AnnotationLayer
{
    std::string id;       // stable; the defaults use fixed ids
    std::string name;
    bool visible = true;
    bool locked = false;
    double opacity = 1.0; // [0, 1], multiplies stroke and fill alpha
};
```

- A scene's layers are an **ordered list**; index 0 is the **bottom**. Order is
  z-order: a higher layer paints on top and is hit first.
- **Defaults**: `Diagnostic` (id `diagnostic`), `Teaching` (`teaching`),
  `Research` (`research`), in that order bottom to top, all visible, unlocked,
  opaque. A scene whose file says nothing about layers has exactly these.
  Fixed ids for the defaults are what lets a layer-less file map onto them.
- `Annotation` gains a `layerId` (not part of `AnnotationProperties`: the
  properties panel edits properties; a layer is where the annotation lives).
- **Resolution rule** (`core::effectiveLayerId(annotation, layers)`): an
  annotation's `layerId` if the scene has that layer, otherwise the **bottom
  layer's**. An empty `layerId` (every annotation saved before this change)
  resolves to Diagnostic. This one rule is what makes "never on no layer"
  hold for old files, hand-edited files, imports and undo alike — every reader
  of a layer goes through it.
- **Last layer**: a scene always has at least one layer; the last cannot be
  deleted.
- `AnnotationModel` holds the scene's layers and the active layer id beside the
  annotations, emits `layersChanged`, and stamps `layerId` in `add()` exactly as
  it stamps the counting category — keyed on the model's state, not on a tool.
  `clear()` / `replaceAll()` reset layers with the annotations.

## 4. The file — and the version rule

The schema gains a top-level `"layers"` array (`id`, `name`, `visible`,
`locked`, `opacity`; order is array order) and a per-annotation `"layerId"`.

**`schemaVersion` is written as 2 only when the layers carry information, and
as 1 otherwise.** "Carry information" means any of: an annotation resolved to a
layer other than Diagnostic; a layer list that is not exactly the three
defaults with default settings (renamed, added, deleted, reordered, hidden,
locked, faded). Otherwise the `layers` key and every `layerId` are **omitted**,
and the file is byte-for-byte what this build wrote before layers existed.

Why conditional, and why the version moves at all — against the rule that a
new geometry type does not move it (CLAUDE.md):

- A new *shape* fails on that shape alone in an older build, by name, and the
  file is left intact. A layer assignment does not fail anywhere: an older
  build reads the file, ignores `layers` and `layerId` as unknown keys, and its
  next autosave writes them away. Every teaching mark lands on Diagnostic and
  every hidden answer becomes visible. That is silent data loss, which only a
  version refusal prevents: an older build refuses a version it does not know,
  outright, and leaves the file intact.
- Writing 2 unconditionally would make every older build refuse every file
  this build touches, during a staggered rollout, for files that use no layers
  at all. Conditional writing keeps the refusal to the files where it protects
  something.
- The cost, stated: the first time a user moves an annotation to Teaching or
  hides a layer, that scene's file stops opening in older builds. That is the
  intended trade.

This build reads versions 1 and 2. On read, a version-1 file gets the default
layers. A version-2 file's layers are validated — unique non-empty ids, a name,
opacity in [0, 1] — and a file that fails is refused like any other invalid
field, so the "never write what we cannot read" invariant holds.

## 5. Behaviour

| | Hidden layer | Locked layer |
|---|---|---|
| Painted on the slide | no | yes, at the layer's opacity |
| Hit by a click / selectable in the viewport | no | yes (to read it) |
| In the annotation list | **no** (it is a view of what is shown, and a teaching layer is hidden precisely so it is not read) | yes, with a padlock |
| Counted in the tally and both exports | **yes** — counts never follow visibility | yes |
| Move, resize, vertex edit, delete, property edits, Move to Layer | — | **refused**, with the status-bar reason "Layer *X* is locked" |
| Drawing while it is the active layer | refused: "Layer *X* is hidden" | refused: "Layer *X* is locked" |

- **The tally says when it is counting what you cannot see**: under the strip,
  "Includes *n* on hidden layers" whenever *n* > 0. The clinical count stays
  whole and the view stays honest.
- **Selection**: hiding the layer of the selected annotation clears the
  selection (a selection names something visible, as it names something the
  model holds); vertex editing and the text editor on it end the same way.
- **Opacity** multiplies the annotation's stroke and fill alpha and a text
  plate's; it does not change the stored colour.
- **Z-order**: painting and hit-testing use one ordering — layers bottom to top,
  model order within a layer — from one pure function, so the painted top is
  the clicked top (the rule the text annotation's `topmostOf` already keeps).
- **Imports** go to the active layer (refused, like drawing, if it is hidden or
  locked).

## 6. The Layers tab

A tab beside Annotation Properties, Properties, Metadata and Channels. One row
per layer, **top layer first** (the order a user reads a stack in):

- an **eye** check box (visible), a **padlock** check box (locked), the name,
  the annotation count, an opacity slider;
- the **active** layer in bold with a marker; clicking a row's name makes it
  active (double-click renames);
- buttons: **New**, **Delete** (enabled only for an empty layer, never the
  last), **Merge into…** (moves every annotation of this layer to another — one
  undo step — and leaves this layer, now empty, for the user to delete),
  **Up**, **Down**.

The status bar shows "Layer: *name*" for the active layer. On a read-only scene
the tab shows the layers and lets the user toggle **visibility** — a view, and
the one thing a reader needs — but every other control is disabled, as the
drawing tools are (the change could not be saved).

**Undo**: every change to an annotation's layer is an undo step — Move to Layer
("Move to Teaching"), Merge ("Merge Research into Diagnostic"), one step each,
through a many-annotation command like the category retag (`restoreMany`, not
`modelReset`, for the reason that command records). Layer **structure** —
create, rename, delete an empty layer, reorder, visibility, lock, opacity — is
not on the undo stack: it changes no annotation, and each control is its own
way back. Undoing a move whose layer has since been deleted puts the annotation
on the bottom layer (§3's resolution rule), never nowhere.

**Autosave**: every layer change — structure or assignment — marks the scene
dirty and is saved by the existing debounced autosave.

## 7. Exports and imports

- GeoJSON export: each feature's `slideio` block gains `"layer": "<name>"`.
  QuPath has no layer concept. The import ignores it (imports go to the active
  layer, §5); mapping by name is a later choice.
- Counts and measurements CSVs: unchanged, every layer included (§5).
- ASAP XML import: unchanged; to the active layer.

## 8. Code shape

- **core**: `AnnotationLayer.{h,cpp}` — the struct, `defaultLayers()`,
  `effectiveLayerId`, `layersCarryInformation(layers, annotations)` (decides
  the version), `paintOrder(annotations, layers)` (indices bottom to top),
  `isValidLayerList`. `Annotation` gains `layerId`; `AnnotationDocument` gains
  `layers`; the serializer writes and reads per §4. `kCurrentSchemaVersion`
  becomes 2 (the highest this build reads); the written version is chosen per
  file.
- **app**: `AnnotationModel` — layers, active layer, `layersChanged`,
  `setLayers`, `moveToLayer(ids, layerId)`; `hitTest` honours visibility and
  `paintOrder`; an `AnnotationLayerMoveCommand` (or the retag command
  generalised) for undo. The persistence service saves layer changes.
- **ui**: `LayersPanel` (dock tab) with its rules in a pure `LayersPanelPolicy`
  (which buttons are enabled, row order, the merge target list);
  `ViewportWidget` paints in `paintOrder`, skips hidden layers, applies opacity,
  refuses edits on locked layers through one `annotationLocked(id)` check
  beside `annotationsEditable()`; the list panel filters hidden layers and marks
  locked ones; the counting strip's hidden-layers line; Move to Layer in both
  context menus; the status-bar label.

## 9. Tests

- core: version 1 written for default layers and an all-Diagnostic scene (byte
  for byte the pre-change output for an existing fixture); version 2 with
  `layers` and `layerId` once anything differs; round trip of a v2 file; a v1
  file reads as the three defaults with every annotation on Diagnostic; an
  unknown `layerId` resolves to the bottom layer; invalid layer lists refused
  (duplicate id, empty id, opacity 1.5); a version-3 file still refused;
  `paintOrder` bottom-to-top then model order.
- app: `add()` stamps the active layer; `moveToLayer` + undo/redo; merge as one
  step; hit test skips hidden layers and respects layer order; `clear()` resets
  layers to the defaults.
- ui: tally counts hidden-layer annotations and reports the hidden count; the
  list rows omit hidden layers; `LayersPanelPolicy` — Delete enabled only on an
  empty non-last layer, merge targets exclude the source, Up/Down at the ends.

## 10. Manual checks — group AM

Written before the run.

| | Check | Result |
|---|---|---|
| AM1 | A slide with existing annotations opens with Diagnostic, Teaching, Research; every annotation is on Diagnostic; the file is unchanged on disk until something is changed. | |
| AM2 | Make Teaching active (status bar says so), draw a rectangle: it is on Teaching. The file is now version 2 with `layers` and `layerId`. | |
| AM3 | Hide Teaching: its annotations vanish from the slide and the list, a click there selects nothing, the tally still counts them and says "Includes n on hidden layers". | |
| AM4 | Lock Diagnostic: its annotations show a padlock in the list; move, resize, Delete and property edits are refused with the reason. Drawing with Diagnostic active is refused. | |
| AM5 | Opacity 30% on Research: its annotations fade; the stored colour is unchanged in the properties panel. | |
| AM6 | Move to Layer from the viewport and the list menus; undo puts it back; one step each. | |
| AM7 | Reorder: Research above Diagnostic paints on top and is hit first where they overlap; move it below and the reverse. | |
| AM8 | New, rename, merge (Research into Diagnostic, one undo step), delete the emptied layer; Delete is disabled on a non-empty layer and on the last layer. | |
| AM9 | Close and reopen the slide: layers, settings and assignments come back. | |
| AM10 | A read-only second window: visibility works, every other layer control is disabled. | |
| AM11 | Export Counts and Measurements with a hidden layer: its annotations are in both. GeoJSON carries `slideio.layer`. | |
| AM12 | The pre-change build: a version-2 file is refused (version too new) and left byte for byte; a version-1 file written by this build after edits that use only Diagnostic still opens. | |

## 11. Left to the builder

- Icons for the eye and padlock (Qt standard icons or text glyphs).
- Whether rename is in place (double-click) or a small dialog.
- Drag-and-drop reordering in addition to Up/Down — optional.
