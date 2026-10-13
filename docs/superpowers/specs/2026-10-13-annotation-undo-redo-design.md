# Annotation undo/redo

**Date:** 2026-10-13
**Status:** Approved, not implemented
**Covers:** `main` @ `38488e9`
**Sub-project:** D (undo/redo)
**Predecessors:** `2026-10-08-annotation-foundation-design.md`,
`2026-10-10-annotation-persistence-design.md`,
`2026-10-12-annotation-geometry-widening-design.md`
**Open items this closes or moves:**
`2026-10-11-annotation-persistence-followups.md` item 7 (closes), item 8 (moves
to sub-project F, unchanged)
**Requirements:** FR-ANN-18 (Must — undo/redo stack, minimum 50 levels,
per-slide); UI design §7.4; architecture §10.7

---

## 1. Intent

Annotation persistence protects a pathologist's work from the application
failing: a file that cannot be parsed is never overwritten, a failed save is
retried and reported, a slide switch flushes before it resets. All of that
defends against the software going wrong.

Nothing defends against the user. Drag a 32-vertex polygon off the lesion by
accident and there is no way back to where it was. Press Delete on the wrong
annotation and it is gone. These are not edge cases reached by a crash or a
corrupt file — they are ordinary, correct use of the tools as built, and they
are the last remaining way to lose work in this subsystem.

This increment adds undo and redo over annotation operations. It is
deliberately narrow: the goal is that no single mis-click costs the user
anything, not that the application gains a general command infrastructure.

### 1.1 What is in scope

UI §7.4 lists the tracked operations as create, delete, move, resize, reshape,
property change, paste and layer change. Of those, **only create, delete and
move have an implementation to undo.** There is no property editing, no
clipboard, no layers, no vertex editing and no resize handle in the viewer
today. The tracked set is therefore:

| operation | reached by |
|-----------|-----------|
| create | committing a rectangle, polygon or freehand trace |
| delete | Delete on the selected annotation |
| move | dragging a committed annotation and releasing |

The design below extends to the rest without new command types; see §3.2.

### 1.2 What this is not

Navigation is not undoable. UI §7.4 and §3 of the UI design are explicit that
`Ctrl+Z` undoes annotation edits while `Ctrl+Left` moves through viewport
history, and the onboarding hints say so to the user. Nothing here changes
that.

---

## 2. Decisions taken before design

Three questions were settled in discussion with the project owner and are
recorded here because the reasoning does not survive in the code.

**The stack is cleared whenever the slide or scene changes.** Not retained
per (slide, scene). UI §7.4's "switching slides switches the undo stack" admits
both readings; clearing is chosen because a retained stack holds commands
referring to annotations that have since been cleared from the model and
re-read from disk. The ids survive that round trip, so a retained stack could
be made to work — but it would be correct only for as long as nobody changed
the load path, and the failure mode is an undo that silently resurrects or
destroys the wrong shape. Switching away and back gives a fresh, empty stack.

**`Ctrl+Z` during a live trace removes the last placed vertex.** It does not
touch the undo stack, because a pending multi-click shape is not an annotation
— it never reaches `AnnotationModel` and is never saved. Vector editors
(Illustrator, Inkscape) step back one anchor point on `Ctrl+Z` during path
drawing, so this is the instinct users arrive with. It makes `Ctrl+Z`
context-dependent, which is the cost; the alternative is that the obvious key
for "take that point back" does nothing and the user must discover Backspace.
Once the trace commits, `Ctrl+Z` means undo-the-annotation again.

**One generic command, not a class per operation.** See §3.

---

## 3. Architecture

### 3.1 Layer placement

`ICommand` and `UndoRedoStack` live in the **application layer**
(`slideio::viewer::app`), as architecture §10.7 specifies and as
`AnnotationModel` already does. They need no Qt beyond what that layer already
links, and no widget.

The stack is owned by `MainWindow::Impl`, beside the persistence service.
`ViewportWidget` is given a pointer to it, as it is for the persistence
service today.

### 3.2 One command type

§10.7 sketches `ICommand` with `execute()`, `undo()` and `description()`, which
leaves the concrete commands open. Every annotation operation has the same
shape — one annotation id, its state before, its state after:

```cpp
namespace slideio::viewer::app {

class AnnotationEditCommand : public ICommand
{
public:
    /// `before` absent  => the operation created the annotation.
    /// `after`  absent  => the operation deleted it.
    /// Both present     => the operation modified it in place.
    /// Neither is a programming error and is rejected in the constructor.
    AnnotationEditCommand(AnnotationModel& model,
                          std::string id,
                          std::optional<core::Annotation> before,
                          std::optional<core::Annotation> after,
                          std::string description);

    void execute() override;   // applies `after`
    void undo() override;      // applies `before`
    [[nodiscard]] std::string description() const override;
};

} // namespace
```

Applying a state is one rule: **if the state is present, put the annotation in
the model under its id; if it is absent, remove that id.**

This covers create, delete and move today. It covers property changes,
reshaping and vertex editing unchanged when those arrive, because they are all
"the annotation was this, now it is that". Multi-annotation operations (paste,
delete-many) need a compound command holding a vector of edits applied as one;
that is not built here because nothing produces them yet.

The alternative — `CreateAnnotationCommand`, `DeleteAnnotationCommand`,
`MoveAnnotationCommand` — was rejected. The inverse logic is identical in all
three, which is the signal that it wants to be one class, and each new
operation would otherwise bring a new class with its own chance of getting the
inverse backwards.

### 3.3 Redo must preserve the id

`AnnotationModel::add()` mints a fresh id. A create command therefore captures
the stored annotation **after** the add (via `find()`, copied immediately —
that pointer is invalidated by any mutation), and its redo path uses
`insert()`, which stores the annotation under the id it already carries.

If redo used `add()` the annotation would come back with a different id on
every cycle, and any other command in the stack referring to it would dangle.
This is the single most likely way to get this wrong and it is invisible until
a second command touches the same annotation.

`insert()` drops silently when the id is already present, so the apply rule in
§3.2 removes any existing entry for the id before inserting. Undo of a move
therefore does not depend on `setGeometry`, and one code path serves all three
operations.

### 3.4 The stack

As §10.7, with the depth cap applied at the oldest end:

```cpp
class UndoRedoStack
{
public:
    explicit UndoRedoStack(int maxDepth = kDefaultUndoDepth);  // 50

    void push(std::unique_ptr<ICommand> command);  // truncates the redo tail
    void undo();
    void redo();
    void clear();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] std::string undoDescription() const;
    [[nodiscard]] std::string redoDescription() const;
};
```

`push()` does **not** execute the command. The four call sites push after the
mutation has already happened through the model's own API — the command
records what was done rather than performing it. This keeps the drawing code
reading as it does now, and means a bug in the command cannot corrupt the
first, live application of an edit; only its undo.

FR-ANN-18 requires a minimum of 50 levels; §7.4 allows configuring up to 200.
`kDefaultUndoDepth` is 50. **The preference is deferred** — it is a settings
row and a spin box, independent of everything here, and bundling it widens the
change for no gain in capability.

---

## 4. Where commands are created

Four sites, all in `ViewportWidget`, each at the point an operation
*completes*:

| site | command | `before` | `after` |
|------|---------|----------|---------|
| `commitPendingShape` | create — polygon / freehand | absent | the stored annotation |
| rectangle drag release | create — rectangle | absent | the stored annotation |
| Delete key handler | delete | the annotation, copied before removal | absent |
| move drag **release** | move | `moveOriginalGeometry` applied to the annotation | its state at release |

### 4.1 Two deliberate non-sites

**The per-frame `setGeometry` during a move drag pushes nothing.** A move drag
calls `setGeometry` on every mouse-move event — that is live preview, not a
sequence of edits. A command per call would fill the stack with hundreds of
micro-moves from one gesture and push the user's real history off the end.
`ViewportWidget::Impl::moveOriginalGeometry` already holds the press-time
geometry as a single source of truth (Escape restores from it), so the move
command's `before` is available at release for free.

**Escape-cancelling a move pushes nothing.** It restores
`moveOriginalGeometry` itself, so there is no net change to record. Pushing a
command there would put a no-op on the stack that the user would have to press
`Ctrl+Z` through.

### 4.2 Descriptions

`description()` returns text fit for a menu item and a status message: "Draw
Polygon", "Delete Rectangle", "Move Freehand Line". The shape name comes from
`core::typeOf()` on the geometry, so it cannot disagree with what was drawn.

---

## 5. The viewport stops being the only mutator

`ViewportWidget` repaints after a change because it is the only thing that
mutates the model. Undo breaks that assumption: an undone create must vanish
from the screen without the widget having done anything.

`AnnotationModel` already emits `annotationAdded`, `annotationRemoved`,
`annotationChanged`, `selectionChanged`, `cleared` and `modelReset`.

**No widget listens to any of them.** `AnnotationPersistenceService` already
consumes four — `annotationAdded`, `annotationRemoved`, `annotationChanged`
and `cleared` — which is how an edit marks the document dirty (§6.5);
`modelReset` and `selectionChanged` have no listener at all. So the signals are
live and exercised, but nothing in the presentation layer has ever reacted to
one.

This increment connects all six to a slot in `ViewportWidget` that calls
`update()`.

This closes item 7 of the persistence follow-ups, which has been held open
waiting for exactly this second mutator.

**The existing direct `update()` calls stay.** They are correct, Qt coalesces
repaints within an event-loop turn so the duplicate costs nothing, and removing
them would mean auditing each one to confirm it was only ever about the model.
Tidiness is not worth that risk.

**Item 8 — the model being owned by a presentation widget — is deliberately
not fixed here.** `ViewportWidget` constructs it (`ViewportWidget.cpp:1933`).
Moving ownership to `MainWindow::Impl` buys nothing until a second *consumer*
exists, and the follow-ups already say F (panels) is where that pays off. This
is a scope boundary, not an oversight.

### 5.1 Undo and redo are refused while a drag is in flight

The widget caches `movingId` and pending-shape state. An undo that removed the
annotation currently being dragged would leave those pointing at nothing.
Rather than defend every cached id, the handler does nothing when a tool drag,
a move drag or a pending trace with the button held is active.

The case this actually affects is narrow — pressing `Ctrl+Z` with the mouse
button down — and doing nothing is the obviously right answer there. Mid-trace
`Ctrl+Z` without a button held is the vertex removal of §2.

### 5.2 Selection follows the operation

`remove()` clears the selection when it named the removed annotation, so
undoing a delete would otherwise bring the shape back unselected. After an undo
or redo the selection is set to the affected annotation when it exists in the
model afterwards, and cleared otherwise.

An undone delete therefore comes back selected and an undone create leaves
nothing selected. This costs one line and it tells the user what just changed,
which matters when the annotation is outside the current view.

---

## 6. Integration

### 6.1 Menu and shortcuts

A new **Edit** menu, between File and View:

| item | shortcut |
|------|----------|
| Undo *\<description\>* | `Ctrl+Z` |
| Redo *\<description\>* | `Ctrl+Y`, and `Ctrl+Shift+Z` |

These are **window-level menu shortcuts**, not viewport-scoped. Tool shortcuts
use `Qt::WidgetWithChildrenShortcut` on the viewport and need viewport focus;
undo must work regardless of which child has focus, as `Ctrl+0` does.

Both items carry the description of what they would act on and are disabled
when the corresponding stack end is empty. Disabled now reads as disabled —
`main.cpp` dims disabled menu text application-wide as of `99f5a28`, without
which this feedback would be invisible.

### 6.2 One branch in the handler

```
if the viewport has a live trace:  remove its last vertex
else:                              stack.undo()
```

A single entry point for the key, with the context test in one place rather
than split across a shortcut and a key handler.

### 6.3 The read-only gate needs no code

On a read-only slide nothing mutates: the drawing tools are disabled and the
inner guard blocks drag and Delete. No command is ever pushed, the stack stays
empty, and the menu items are disabled by that alone. A slide cannot become
read-only without a reopen, and a reopen clears the stack (§6.4).

### 6.4 Clearing

The stack is cleared on `AnnotationModel::cleared()` and `modelReset()` — the
two signals that fire only on slide-change and load paths. Hanging the clear
off the model rather than calling it at each slide-switch site means a future
way to switch slides cannot forget it.

### 6.5 Persistence needs no work

An undo mutates the model; `AnnotationPersistenceService` already listens and
marks the document dirty; autosave writes it. Its `m_loading` guard means the
load path still does not mark dirty. The behaviour is correct by construction.

An undone state is a state the user chose, so it is the one to persist. Undoing
back to the last-saved state leaves the document dirty and rewrites an
identical file, which is harmless.

### 6.6 Feedback: a status message, not a toast

UI §7.4 asks for "a brief toast notification near the toolbar showing the
undone/redone action, visible for 2 seconds". This design uses
`StatusBarManager::showTransientMessage` instead — "Undo: Delete Polygon" —
because that is where every other transient message in this application goes,
and building a toast widget for a single caller is not justified.

**Recorded as a conscious departure from §7.4**, not an omission. If a toast is
wanted later it is a presentation change with no effect on anything here.

---

## 7. Testing

The part most likely to be wrong is the inverse logic: a create whose redo
mints a new id, a move whose before and after are swapped, a depth cap that
drops the wrong end. All of it is in the application layer against a real
`AnnotationModel`, and `tests/app/AnnotationModelTest.cpp` proves that is
constructible in a suite.

### 7.1 Automated — `app-tests`

**Stack:**
- `push` truncates the redo tail; `canRedo()` false afterwards.
- Depth cap drops the **oldest** command, not the newest; `canUndo()` stays
  true at the cap.
- `canUndo`/`canRedo` and the two descriptions at the ends and when empty.
- `clear()` empties both directions.

**Commands, against a real model:**
- create → undo removes it → redo restores it **under the same id**.
- delete → undo restores geometry *and* metadata (author, timestamps) exactly.
- move → undo returns the geometry to the original, vertex for vertex.
- a 32-vertex polyline survives an undo/redo cycle vertex-exact, as the
  serializer round-trip test does for the same shape.
- undo/redo alternated several times is stable, not drifting.
- a command whose `before` and `after` are both absent is rejected.

### 7.2 Manual

No suite creates a `QApplication`, so no widget can be constructed in a test
and none of the wiring is reachable from one. These checks are added to item 0
of `2026-10-11-annotation-persistence-followups.md` as group **U**, alongside
the existing P-checks, because that list is the only coverage this layer has
and it has just been proven to work:

- **U1** Draw a polygon, `Ctrl+Z` — it disappears. `Ctrl+Y` — it returns with
  the same vertices over the same tissue.
- **U2** Delete an annotation, `Ctrl+Z` — it returns **selected**.
- **U3** Move an annotation, `Ctrl+Z` — it returns to its exact original
  position, not approximately.
- **U4** One move drag produces **one** undo step, not one per mouse-move
  frame. Press `Ctrl+Z` once and the annotation is fully back.
- **U5** `Ctrl+Z` mid-trace removes the last vertex and the trace stays live;
  it does not touch a previously committed annotation.
- **U6** `Ctrl+Z` with the mouse button held during a drag does nothing.
- **U7** Escape-cancel a move, then `Ctrl+Z` — the *previous* operation is
  undone, not a no-op step.
- **U8** Switch slides and back: Edit ▸ Undo is greyed out.
- **U9** On a read-only slide both Edit items are greyed out.
- **U10** Undo, then let autosave run, then reopen the slide: the undone state
  is what loads from disk.
- **U11** The Edit menu items name the operation ("Undo Delete Polygon") and
  the status bar shows it.

---

## 8. Risks

**An undo that silently does nothing is worse than no undo.** The user presses
`Ctrl+Z`, sees no change, assumes it worked, and keeps going. The drag guard
(§5.1) is the one place this design deliberately does nothing, which is why it
is restricted to the narrowest possible case.

**The id-minting trap (§3.3)** is invisible in single-operation testing and
only shows when a second command touches the same annotation. U1's redo
explicitly checks the id, and the automated create/undo/redo test asserts it.

**Repaint now depends on signals no widget has ever consumed.** Four of the six
are already driving the persistence service, so they are known to fire; but
`modelReset` and `selectionChanged` have never had a listener at all, and no
repaint has ever been triggered by any of them. A missed connection shows as a
stale screen after an undo — visible rather than silent, and covered by
U1-U3.

**Depth 50 of long freehand traces is real memory.** Each command holds up to
two copies of one annotation. A 2000-point trace is roughly 32 KB per copy, so
a pathological stack is a few megabytes — acceptable, and bounded by the cap.
Worth knowing before the depth preference raises it to 200.

---

## 9. Out of scope, recorded

- The **depth preference** (50–200, UI §7.4). Deferred; see §3.4.
- The **toast notification**; replaced by a status message, §6.6.
- **Item 8**, model ownership; stays with sub-project F, §5.
- **Compound commands** for multi-annotation operations; nothing produces them
  until paste or multi-select exists, §3.2.
- **Vertex editing**, resize handles, property editing and layers. When they
  arrive they need no new command type, only new push sites.
