# Metadata dock pane — design

**Date:** 2026-05-10
**Status:** Approved (proceeding to implementation plan)

## Goal

Add a docking panel to SlideIO Viewer that shows the metadata of the currently open slide and its active scene as a navigable tree. Metadata is sourced from the SlideIO library's typed `Slide::getMetadata()` / `Scene::getMetadata()` accessors (which return a parsed `slideio::Metadata` tree, not a raw string).

## Non-goals

- No editing of metadata.
- No persistence/export of metadata to disk.
- No re-parsing of raw XML/JSON strings — the library already returns a structured tree.
- No live refresh while the slide is open beyond what scene switching already emits via `slideOpened`.

## Architecture

The viewer enforces strict downward layering: domain (`core`) is pure C++ with no Qt or SlideIO; UI never links SlideIO directly. Therefore the typed `slideio::Metadata` tree must be converted to a layer-neutral representation inside the infra adapter, then carried through `core::SlideInfo` to the UI.

```
slideio::Metadata          (SlideIO library, infra-only include)
        │
        ▼  walked once at slide open
core::MetadataNode         (pure-C++ tree in core)
        │
        ▼  carried inside core::SlideInfo
ui::MetadataPanel          (QDockWidget + QTreeWidget)
```

## 1. Data flow (core + infra)

### 1.1 New core type — `core::MetadataNode`

New header `src/core/include/slideio/viewer/core/Metadata.h`:

```cpp
namespace slideio::viewer::core {

struct MetadataNode
{
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    std::string name;          // key for object children, "[i]" for array items, empty at root
    Type type = Type::Null;
    std::string value;         // pre-formatted scalar text ("42", "3.14", "true", "hello")
    std::vector<MetadataNode> children;
};

} // namespace slideio::viewer::core
```

The `value` field holds a pre-formatted string for scalar types, so the UI does not need to know about typed accessors. Object/array nodes have `children` populated; their `value` field is unused (the UI synthesizes a summary like `{N keys}` / `[N items]` for display).

### 1.2 Extend `core::SlideInfo`

In `src/core/include/slideio/viewer/core/Types.h`:

```cpp
struct SlideInfo
{
    // ... existing fields ...
    MetadataNode slideMetadata;   // populated from Slide::getMetadata()
    MetadataNode sceneMetadata;   // populated from Scene::getMetadata()
};
```

Both default to `Type::Null` when the slide/scene reports no metadata. The UI treats `Null` at the root specially (shows "(no metadata)").

### 1.3 SlideIOAdapter populates the trees

In `src/infra/src/SlideIOAdapter.cpp`, both constructors (regular scene and aux image) gain a population step at the end of slide-info construction:

```cpp
m_slideInfo.slideMetadata = convertMetadata(m_slide->getMetadata(), /*name=*/"");
m_slideInfo.sceneMetadata = convertMetadata(m_scene->getMetadata(), /*name=*/"");
```

Where `convertMetadata` is a file-local recursive helper that walks `slideio::Metadata` and returns a `core::MetadataNode`:

- `Type::Object` → iterate `keys()`, recurse on each `meta[key]` with that key as the child name.
- `Type::Array` → iterate `0..size()-1`, recurse on each `meta[i]` with `"[i]"` as the child name.
- `Type::Bool` → leaf, `value = meta.asBool() ? "true" : "false"`.
- `Type::Int` → leaf, `value = std::to_string(meta.asInt())`.
- `Type::Double` → leaf, `value = format(meta.asDouble())` formatted with `%g`-style up to 15 significant digits (Qt: `QString::number(d, 'g', 15).toStdString()` or equivalent `<charconv>` call), so integer-valued doubles print without trailing zeros and full-precision values are not truncated.
- `Type::String` → leaf, `value = meta.asString()`.
- `Type::Null` → leaf, no value.

The function catches exceptions per-subtree so a single malformed branch does not abort the whole walk; the offending subtree becomes a `Null` node and a warning is logged via spdlog.

### 1.4 No change to `ISlideSource`

The two trees travel inside the existing `slideInfo()` payload. No new virtual methods are added to the abstract interface.

## 2. UI panel — `MetadataPanel`

New files in the `ui` module:

- `src/ui/include/slideio/viewer/ui/MetadataPanel.h`
- `src/ui/src/MetadataPanel.cpp`

### 2.1 Public surface

```cpp
namespace slideio::viewer::ui {

class MetadataPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit MetadataPanel(QWidget* parent = nullptr);
    ~MetadataPanel() override;

    MetadataPanel(const MetadataPanel&) = delete;
    MetadataPanel& operator=(const MetadataPanel&) = delete;

    void setSlideInfo(const core::SlideInfo& info);
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
```

PIMPL idiom matches the project convention.

### 2.2 Internal layout and behavior

- **Widget:** a two-column `QTreeWidget` (`Property` / `Value`) configured identically to `SlidePropertiesPanel` (read-only, alternating row colors, single-row selection, `ResizeToContents` on column 0, stretchLastSection).
- **Top-level items:** two parents added on every `setSlideInfo` call: `"Slide"` and `"Scene"`. Each is populated by a recursive `addNode(QTreeWidgetItem* parent, const core::MetadataNode& node)` helper:
  - `Object` → item shows `{N keys}` in column 1, children added recursively for each key.
  - `Array` → item shows `[N items]` in column 1, children added recursively with synthetic names `[0]`, `[1]`, ….
  - `Bool` / `Int` / `Double` / `String` → leaf, `node.value` shown in column 1.
  - `Null` → leaf, `"(null)"` shown in column 1.
- **Empty trees:** if the entire subtree under "Slide" or "Scene" is `Type::Null`, that parent shows `"(no metadata)"` in column 1 and has no children.
- **Initial expansion:** both top-level parents start expanded; nested object/array children start collapsed. Real-world TIFF/CZI metadata trees can be deep, so collapsed-by-default keeps the panel compact.
- **Right-click → "Copy as text":** matches `SlidePropertiesPanel`. Walks the full visible tree (regardless of expanded state) and emits an indented `Property: Value` listing to the system clipboard, with each level indented two spaces.
- **Dock setup:** `objectName = "MetadataPanel"` so `QMainWindow::saveState/restoreState` persists position. Title `"Metadata"`. `setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea)`.

### 2.3 Refresh behavior

The panel state is purely a function of the latest `core::SlideInfo`. Scene switching in the existing code path emits `slideOpened` with fresh slide info, so when the user picks a new scene in the Scenes panel the Metadata panel refreshes automatically — no extra signal/slot wiring is needed.

Aux images opened via the Associated Images panel pop into a separate `AssociatedImageWindow` and do not switch the active scene; they therefore do not refresh the Metadata panel, which continues to show the active main scene's metadata. This is the intended behavior.

## 3. MainWindow wiring

In `src/ui/src/MainWindow.cpp`:

- `MainWindow::Impl` gains:
  ```cpp
  MetadataPanel* metadataPanel = nullptr;
  QAction* metadataToggleAction = nullptr;
  ```
- Construction next to `propertiesPanel`:
  ```cpp
  m_impl->metadataPanel = new MetadataPanel(this);
  addDockWidget(Qt::RightDockWidgetArea, m_impl->metadataPanel);
  m_impl->metadataPanel->hide();
  ```
- Toggle action set up alongside `propertiesToggleAction`:
  ```cpp
  m_impl->metadataToggleAction = m_impl->metadataPanel->toggleViewAction();
  m_impl->metadataToggleAction->setText("Meta&data");
  m_impl->metadataToggleAction->setShortcut(QKeySequence("Ctrl+Shift+D"));
  m_impl->metadataToggleAction->setStatusTip("Toggle the slide/scene metadata panel");
  ```
  Shortcut chosen as `Ctrl+Shift+D` — `M`/`P`/`C`/`A`/`T` are already taken by minimap / properties / channels / associated images / scenes; `D` for "data" is free.
- View menu gains the action right after `propertiesToggleAction`.
- The `slideOpened` lambda (`MainWindow.cpp`) gains, after `propertiesPanel->setSlideInfo(info);`:
  ```cpp
  metadataPanel->setSlideInfo(info);
  ```
- The `slideClosed` lambda gains, after `propertiesPanel->clear();`:
  ```cpp
  metadataPanel->clear();
  ```

The panel starts hidden on first launch; once toggled on, Qt's `saveState/restoreState` remembers visibility, dock area, and floating state across runs (same mechanism the other panels rely on).

## 4. Build system changes

- `slideio-viewer-core` CMake target adds `include/slideio/viewer/core/Metadata.h` to its installed headers / interface (header-only, no `.cpp`).
- `slideio-viewer-ui` CMake target adds the new `MetadataPanel.h` / `MetadataPanel.cpp`.
- `slideio-viewer-infra` gains no new files (the recursive helper is file-local inside `SlideIOAdapter.cpp`).
- No new third-party dependencies.

## 5. Testing

The viewer codebase does not yet have automated unit tests for UI panels — the existing `SlidePropertiesPanel` is also untested. We follow the same precedent here: implementation is verified by manual smoke testing on slides with diverse metadata (TIFF/SVS/CZI/NDPI) covering:

- A slide whose `Slide::getMetadata()` returns `Null` → "(no metadata)" under "Slide".
- A slide with rich object/array metadata (e.g., CZI) → tree expands cleanly, no truncation.
- Scene switching in a multi-scene file → `Scene` subtree updates to the new scene's metadata.
- Right-click "Copy as text" → clipboard contains the full indented listing.
- Dock visibility/position survives application restart.

If a `convertMetadata` unit test target is added in a future iteration, it would live in `tests/` against pure `core::MetadataNode` fixtures (the converter itself is the only code with real branching logic).

## 6. Risks and mitigations

- **Very large metadata trees** (some CZI files have thousands of nodes). Mitigation: keep nested children collapsed by default; populating a `QTreeWidget` with a few thousand items is well within Qt's comfort zone.
- **Exception-throwing accessors on malformed nodes.** Mitigation: per-subtree `try`/`catch` in `convertMetadata` reduces a bad branch to a `Null` leaf and logs a warning; the rest of the tree still renders.
- **Layering drift.** Mitigation: `core::Metadata.h` includes only `<string>` and `<vector>`; `MetadataPanel` includes only `core::SlideInfo` and Qt headers; `slideio::Metadata` is referenced only inside `SlideIOAdapter.cpp`.
