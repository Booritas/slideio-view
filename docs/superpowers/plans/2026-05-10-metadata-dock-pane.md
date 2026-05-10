# Metadata Dock Pane Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `Metadata` dock pane to SlideIO Viewer that shows the active slide's and active scene's parsed metadata as a navigable two-column tree.

**Architecture:** SlideIO returns metadata as a typed tree (`slideio::Metadata`). Because the project enforces strict downward layering (UI must not link SlideIO directly), the tree is converted once inside `SlideIOAdapter` into a pure-C++ `core::MetadataNode`, carried through the existing `core::SlideInfo` payload, and rendered by a new `ui::MetadataPanel` (a `QDockWidget` containing a `QTreeWidget`).

**Tech Stack:** C++17, Qt 6 Widgets (`QDockWidget`, `QTreeWidget`, `QTreeWidgetItem`), CMake, SlideIO library (`slideio::Metadata`), spdlog.

**Reference spec:** `docs/superpowers/specs/2026-05-10-metadata-dock-pane-design.md`

---

## File map

**Create:**
- `src/core/include/slideio/viewer/core/Metadata.h` — pure-C++ `MetadataNode` struct (Qt/SlideIO-free).
- `src/ui/include/slideio/viewer/ui/MetadataPanel.h` — public panel header.
- `src/ui/src/MetadataPanel.cpp` — panel implementation (PIMPL, recursive tree population, copy-as-text).

**Modify:**
- `src/core/include/slideio/viewer/core/Types.h` — include `Metadata.h`, add two new fields to `SlideInfo`.
- `src/infra/src/SlideIOAdapter.cpp` — add anonymous-namespace `convertMetadata` helper, call it in both constructors after `m_slideInfo` is otherwise built.
- `src/ui/CMakeLists.txt` — add `src/MetadataPanel.cpp` to the target sources.
- `src/ui/src/MainWindow.cpp` — declare panel + toggle action, instantiate panel, configure toggle, add to View menu, populate on `slideOpened`, clear on `slideClosed`.

**No changes:**
- `src/core/CMakeLists.txt` — `Metadata.h` is header-only and exposed via the existing `target_include_directories` PUBLIC entry, no source needs to be added.
- `src/infra/CMakeLists.txt` — the helper is file-local in an existing source file.
- `tests/` — feature follows the existing UI-panel precedent (manual smoke test, no automated tests). The conversion helper relies on `slideio::Metadata` whose internals (`Impl`) are opaque, so synthesizing test fixtures without a real slide file is not feasible without invasive refactoring.

---

## Task 1: Create the `core::MetadataNode` struct

**Files:**
- Create: `src/core/include/slideio/viewer/core/Metadata.h`

- [ ] **Step 1: Create `Metadata.h`**

Write `src/core/include/slideio/viewer/core/Metadata.h` with this exact content:

```cpp
#pragma once

#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// Layer-neutral, pre-formatted view of a slide/scene metadata tree node.
/// Populated by infrastructure (SlideIOAdapter) from slideio::Metadata; the
/// UI walks this struct without ever including the SlideIO library headers.
struct MetadataNode
{
    enum class Type
    {
        Null,
        Bool,
        Int,
        Double,
        String,
        Array,
        Object
    };

    /// Key for object children; "[i]" for array items; empty at the root.
    std::string name;

    /// Defaults to Null so a SlideInfo with no metadata renders as
    /// "(no metadata)" in the UI without any explicit population step.
    Type type = Type::Null;

    /// Pre-formatted textual representation for scalar nodes (Bool/Int/
    /// Double/String). Unused for Null/Array/Object — the UI synthesizes
    /// summaries like "{N keys}" / "[N items]" for those.
    std::string value;

    std::vector<MetadataNode> children;
};

} // namespace slideio::viewer::core
```

- [ ] **Step 2: Build the project to confirm the header compiles cleanly**

Run from project root:

```
cmake --build build/build --config Release
```

Expected: build succeeds (no targets actually use the new header yet, but the core target's include directory makes it visible).

- [ ] **Step 3: Commit**

```bash
git add src/core/include/slideio/viewer/core/Metadata.h
git commit -m "Add core::MetadataNode for layer-neutral metadata trees"
```

---

## Task 2: Extend `core::SlideInfo` with the two metadata trees

**Files:**
- Modify: `src/core/include/slideio/viewer/core/Types.h`

- [ ] **Step 1: Add the include and the two new fields**

Open `src/core/include/slideio/viewer/core/Types.h`. Near the top, add the new include after the existing standard-library includes (right after `#include <vector>`):

```cpp
#include "slideio/viewer/core/Metadata.h"
```

Then locate the `struct SlideInfo` block (it ends with `std::vector<LevelInfo> levels;`) and add two new fields immediately after `std::vector<LevelInfo> levels;`:

```cpp
    MetadataNode slideMetadata;   // populated from slideio::Slide::getMetadata()
    MetadataNode sceneMetadata;   // populated from slideio::Scene::getMetadata()
```

After the change, the bottom of `SlideInfo` should read:

```cpp
    std::vector<SceneInfo> scenes;
    std::vector<SceneInfo> auxImages;
    std::vector<LevelInfo> levels;
    MetadataNode slideMetadata;   // populated from slideio::Slide::getMetadata()
    MetadataNode sceneMetadata;   // populated from slideio::Scene::getMetadata()
};
```

- [ ] **Step 2: Build to confirm everything still compiles**

```
cmake --build build/build --config Release
```

Expected: build succeeds. All call sites that pass `SlideInfo` by value/copy now also copy the (currently empty) trees — that's the intended behavior.

- [ ] **Step 3: Commit**

```bash
git add src/core/include/slideio/viewer/core/Types.h
git commit -m "Carry slide and scene metadata trees on core::SlideInfo"
```

---

## Task 3: Populate the trees in `SlideIOAdapter`

**Files:**
- Modify: `src/infra/src/SlideIOAdapter.cpp`

- [ ] **Step 1: Add the SlideIO metadata header include**

Near the existing SlideIO includes at the top of `src/infra/src/SlideIOAdapter.cpp` (around lines 3-7, after `slideio/core/levelinfo.hpp`), add:

```cpp
#include <slideio/core/metadata.hpp>
```

Also add a `<cstdio>` include alongside the other standard-library includes (next to `<cmath>`):

```cpp
#include <cstdio>
```

`<cstdio>` is needed for `std::snprintf`, used to format doubles.

- [ ] **Step 2: Add the `convertMetadata` helper**

Inside the existing anonymous namespace at the top of `SlideIOAdapter.cpp` (the one that already contains `convertSlideIODataType`, `kDefaultTileSize`, and `compressionName`), add the following recursive helper function. Place it after `compressionName` and before the closing `}` of the anonymous namespace:

```cpp
slideio::viewer::core::MetadataNode convertMetadata(
    const ::slideio::Metadata& meta, const std::string& name = {})
{
    using NodeType = slideio::viewer::core::MetadataNode::Type;
    slideio::viewer::core::MetadataNode node;
    node.name = name;

    try {
        switch (meta.type()) {
        case ::slideio::Metadata::Type::Null:
            node.type = NodeType::Null;
            break;
        case ::slideio::Metadata::Type::Bool:
            node.type = NodeType::Bool;
            node.value = meta.asBool() ? "true" : "false";
            break;
        case ::slideio::Metadata::Type::Int:
            node.type = NodeType::Int;
            node.value = std::to_string(meta.asInt());
            break;
        case ::slideio::Metadata::Type::Double: {
            // %.15g — general format, up to 15 significant digits, no
            // trailing zeros for integer-valued doubles. Matches the
            // precision policy in the design doc.
            node.type = NodeType::Double;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.15g", meta.asDouble());
            node.value = buf;
            break;
        }
        case ::slideio::Metadata::Type::String:
            node.type = NodeType::String;
            node.value = meta.asString();
            break;
        case ::slideio::Metadata::Type::Array: {
            node.type = NodeType::Array;
            const size_t n = meta.size();
            node.children.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                node.children.push_back(
                    convertMetadata(meta[i], "[" + std::to_string(i) + "]"));
            }
            break;
        }
        case ::slideio::Metadata::Type::Object: {
            node.type = NodeType::Object;
            const auto keys = meta.keys();
            node.children.reserve(keys.size());
            for (const auto& key : keys) {
                node.children.push_back(convertMetadata(meta[key], key));
            }
            break;
        }
        }
    } catch (const std::exception& ex) {
        // Per-subtree containment: a bad branch becomes a Null leaf so the
        // rest of the tree still renders. The warning is logged once per
        // bad subtree.
        spdlog::warn("SlideIOAdapter::convertMetadata: failed for '{}': {}",
                     name, ex.what());
        node.type = NodeType::Null;
        node.value.clear();
        node.children.clear();
    }

    return node;
}
```

- [ ] **Step 3: Call the helper in the regular-scene constructor**

In the first `SlideIOAdapter` constructor (`SlideIOAdapter::SlideIOAdapter(const std::string& filePath, int sceneIndex, const std::string& driverId)`), find the line:

```cpp
    m_slideInfo.levels = m_levels;
```

Immediately after that line, add:

```cpp
    try {
        m_slideInfo.slideMetadata = convertMetadata(m_slide->getMetadata());
        m_slideInfo.sceneMetadata = convertMetadata(m_scene->getMetadata());
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: getMetadata failed: {}", ex.what());
    }
```

The outer `try/catch` is defensive: if the SlideIO driver itself throws from `getMetadata()` (rather than per-subtree access throwing inside `convertMetadata`), we leave the tree as default-constructed `Null` so the panel shows "(no metadata)".

- [ ] **Step 4: Call the helper in the aux-image constructor**

In the second `SlideIOAdapter` constructor (`SlideIOAdapter::SlideIOAdapter(const std::string& filePath, const std::string& auxImageName, const std::string& driverId)`), find the same line:

```cpp
    m_slideInfo.levels = m_levels;
```

Immediately after that line, add the identical block:

```cpp
    try {
        m_slideInfo.slideMetadata = convertMetadata(m_slide->getMetadata());
        m_slideInfo.sceneMetadata = convertMetadata(m_scene->getMetadata());
    } catch (const std::exception& ex) {
        spdlog::warn("SlideIOAdapter: getMetadata failed: {}", ex.what());
    }
```

- [ ] **Step 5: Build to confirm the adapter compiles**

```
cmake --build build/build --config Release
```

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/infra/src/SlideIOAdapter.cpp
git commit -m "Populate slide and scene MetadataNode trees in SlideIOAdapter"
```

---

## Task 4: Create the `MetadataPanel` skeleton

**Files:**
- Create: `src/ui/include/slideio/viewer/ui/MetadataPanel.h`
- Create: `src/ui/src/MetadataPanel.cpp`

- [ ] **Step 1: Create the public header**

Write `src/ui/include/slideio/viewer/ui/MetadataPanel.h` with this exact content:

```cpp
#pragma once

#include "slideio/viewer/core/Types.h"

#include <QDockWidget>

#include <memory>

namespace slideio::viewer::ui
{

// Read-only docking panel that shows the parsed metadata for the current
// slide and active scene as a two-column tree (Property / Value). The two
// top-level items "Slide" and "Scene" expand into the corresponding
// core::MetadataNode trees carried on core::SlideInfo.
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

- [ ] **Step 2: Create the implementation skeleton**

Write `src/ui/src/MetadataPanel.cpp` with this exact content (skeleton — recursive walk and context menu added in later tasks):

```cpp
#include "slideio/viewer/ui/MetadataPanel.h"

#include <QHeaderView>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace slideio::viewer::ui
{

struct MetadataPanel::Impl
{
    QTreeWidget* tree = nullptr;
};

MetadataPanel::MetadataPanel(QWidget* parent)
    : QDockWidget("Metadata", parent)
    , m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("MetadataPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_impl->tree = new QTreeWidget(this);
    m_impl->tree->setColumnCount(2);
    m_impl->tree->setHeaderLabels(QStringList() << "Property" << "Value");
    m_impl->tree->setRootIsDecorated(true);
    m_impl->tree->setUniformRowHeights(true);
    m_impl->tree->setAlternatingRowColors(true);
    m_impl->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_impl->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_impl->tree->header()->setStretchLastSection(true);
    setWidget(m_impl->tree);
}

MetadataPanel::~MetadataPanel() = default;

void MetadataPanel::clear()
{
    m_impl->tree->clear();
}

void MetadataPanel::setSlideInfo(const core::SlideInfo& /*info*/)
{
    // Population implemented in the next task.
    m_impl->tree->clear();
}

} // namespace slideio::viewer::ui
```

- [ ] **Step 3: Add the new source file to the UI CMake target**

Open `src/ui/CMakeLists.txt`. In the `add_library(slideio-viewer-ui STATIC ...)` source list, add the new file alphabetically near the other panels — insert it on a new line right before `src/MainWindow.cpp` (so the list ends `... src/SlidePropertiesPanel.cpp`, then the new line, then `src/MainWindow.cpp`).

The relevant block becomes:

```cmake
add_library(slideio-viewer-ui STATIC
    src/AppPaths.cpp
    src/AssociatedImageWindow.cpp
    src/DriverFilters.cpp
    src/ViewportController.cpp
    src/ViewportWidget.cpp
    src/MinimapWidget.cpp
    src/ZoomIndicatorWidget.cpp
    src/StatusBarManager.cpp
    src/ChannelMixerPanel.cpp
    src/SceneThumbnailPanel.cpp
    src/ZTNavigationWidget.cpp
    src/LoadingOverlay.cpp
    src/SlidePropertiesPanel.cpp
    src/MetadataPanel.cpp
    src/MainWindow.cpp
)
```

(The CMake target's `file(GLOB_RECURSE _ui_headers …)` line picks up `MetadataPanel.h` automatically for AUTOMOC, so no header listing is needed.)

- [ ] **Step 4: Re-run CMake configure (so the new source is picked up) and build**

```
cmake -S . -B build/build
cmake --build build/build --config Release
```

Expected: configure prints the new file in the target's source list, build succeeds. AUTOMOC processes the `Q_OBJECT` in the header.

- [ ] **Step 5: Commit**

```bash
git add src/ui/include/slideio/viewer/ui/MetadataPanel.h src/ui/src/MetadataPanel.cpp src/ui/CMakeLists.txt
git commit -m "Add MetadataPanel skeleton (empty tree, no population)"
```

---

## Task 5: Implement the recursive tree population

**Files:**
- Modify: `src/ui/src/MetadataPanel.cpp`

- [ ] **Step 1: Add an anonymous-namespace helper that walks `MetadataNode`**

In `src/ui/src/MetadataPanel.cpp`, after the existing `#include` block and *before* `namespace slideio::viewer::ui`, add an anonymous namespace containing two helpers:

```cpp
namespace
{

// Convert a leaf node's stored value to display text. Object/Array nodes
// instead get a synthesized summary ("{N keys}" / "[N items]"). Null gets
// a literal "(null)" so empty leaves are visually distinguishable.
QString valueText(const slideio::viewer::core::MetadataNode& node)
{
    using Type = slideio::viewer::core::MetadataNode::Type;
    switch (node.type) {
    case Type::Null:
        return QStringLiteral("(null)");
    case Type::Bool:
    case Type::Int:
    case Type::Double:
    case Type::String:
        return QString::fromStdString(node.value);
    case Type::Array:
        return QStringLiteral("[%1 items]").arg(node.children.size());
    case Type::Object:
        return QStringLiteral("{%1 keys}").arg(node.children.size());
    }
    return {};
}

void addNode(QTreeWidgetItem* parent,
             const slideio::viewer::core::MetadataNode& node)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, QString::fromStdString(node.name));
    item->setText(1, valueText(node));
    for (const auto& child : node.children) {
        addNode(item, child);
    }
}

// Add a top-level "Slide" or "Scene" parent for the given subtree. If the
// subtree is Null at its root, show a single "(no metadata)" parent with
// no children. Otherwise the parent shows the synthesized summary
// ("{N keys}" / "[N items]") and is expanded by default.
void addRootNode(QTreeWidget* tree, const QString& title,
                 const slideio::viewer::core::MetadataNode& root)
{
    using Type = slideio::viewer::core::MetadataNode::Type;
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, title);
    if (root.type == Type::Null && root.children.empty()) {
        item->setText(1, QStringLiteral("(no metadata)"));
        return;
    }
    item->setText(1, valueText(root));
    for (const auto& child : root.children) {
        addNode(item, child);
    }
    item->setExpanded(true);
}

} // namespace
```

- [ ] **Step 2: Replace `setSlideInfo` to call `addRootNode` for both subtrees**

In the same file, replace the placeholder body of `setSlideInfo` with:

```cpp
void MetadataPanel::setSlideInfo(const core::SlideInfo& info)
{
    m_impl->tree->clear();
    addRootNode(m_impl->tree, QStringLiteral("Slide"), info.slideMetadata);
    addRootNode(m_impl->tree, QStringLiteral("Scene"), info.sceneMetadata);
}
```

- [ ] **Step 3: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/ui/src/MetadataPanel.cpp
git commit -m "Populate MetadataPanel tree from MetadataNode subtrees"
```

---

## Task 6: Add the right-click "Copy as text" context menu

**Files:**
- Modify: `src/ui/src/MetadataPanel.cpp`

- [ ] **Step 1: Add the necessary Qt includes**

At the top of `src/ui/src/MetadataPanel.cpp`, alongside the existing Qt includes, add:

```cpp
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QPoint>
```

- [ ] **Step 2: Add a recursive serializer in the anonymous namespace**

In the anonymous namespace (the one with `valueText` / `addNode` / `addRootNode`), add this helper at the end (right before the closing `} // namespace`):

```cpp
void appendItemAsText(const QTreeWidgetItem* item, int depth, QStringList& out)
{
    QString indent(depth * 2, QLatin1Char(' '));
    out << QStringLiteral("%1%2: %3").arg(indent, item->text(0), item->text(1));
    for (int i = 0; i < item->childCount(); ++i) {
        appendItemAsText(item->child(i), depth + 1, out);
    }
}
```

The full visible tree is serialized regardless of expand/collapse state. Indentation is two spaces per level.

- [ ] **Step 3: Wire the context menu in the constructor**

At the end of `MetadataPanel::MetadataPanel(QWidget* parent)`, after `setWidget(m_impl->tree);`, add:

```cpp
    m_impl->tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_impl->tree, &QWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            QMenu menu(m_impl->tree);
            QAction* copyAction = menu.addAction(QStringLiteral("Copy metadata as text"));
            copyAction->setEnabled(m_impl->tree->topLevelItemCount() > 0);
            QAction* picked = menu.exec(m_impl->tree->viewport()->mapToGlobal(pos));
            if (picked != copyAction) return;

            QStringList lines;
            const int topCount = m_impl->tree->topLevelItemCount();
            for (int i = 0; i < topCount; ++i) {
                appendItemAsText(m_impl->tree->topLevelItem(i), 0, lines);
            }
            QApplication::clipboard()->setText(lines.join(QChar('\n')));
        });
```

- [ ] **Step 4: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/ui/src/MetadataPanel.cpp
git commit -m "Add 'Copy metadata as text' context menu to MetadataPanel"
```

---

## Task 7: Wire the panel into MainWindow

**Files:**
- Modify: `src/ui/src/MainWindow.cpp`

- [ ] **Step 1: Add the include**

At the top of `src/ui/src/MainWindow.cpp`, alongside the other panel includes, add:

```cpp
#include "slideio/viewer/ui/MetadataPanel.h"
```

Place it alphabetically — between `#include "slideio/viewer/ui/MainWindow.h"` block and the other panel includes is fine; in practice, insert immediately before `#include "slideio/viewer/ui/MinimapWidget.h"`.

- [ ] **Step 2: Add the panel and action members to `Impl`**

In `struct MainWindow::Impl`, find the existing line:

```cpp
    SlidePropertiesPanel* propertiesPanel = nullptr;
```

Immediately after it, add:

```cpp
    MetadataPanel* metadataPanel = nullptr;
```

Then find:

```cpp
    QAction* propertiesToggleAction = nullptr;
```

Immediately after it, add:

```cpp
    QAction* metadataToggleAction = nullptr;
```

- [ ] **Step 3: Instantiate the panel in the MainWindow constructor**

In the body of `MainWindow::MainWindow(QWidget* parent)`, find the block that creates the properties panel:

```cpp
    // Create properties dock widget (hidden by default)
    m_impl->propertiesPanel = new SlidePropertiesPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_impl->propertiesPanel);
    m_impl->propertiesPanel->hide();
```

Immediately after that block, add:

```cpp
    // Create metadata dock widget (hidden by default)
    m_impl->metadataPanel = new MetadataPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_impl->metadataPanel);
    m_impl->metadataPanel->hide();
```

- [ ] **Step 4: Configure the toggle action**

Still in the constructor, find the existing toggle-action setup for `propertiesToggleAction`:

```cpp
    m_impl->propertiesToggleAction = m_impl->propertiesPanel->toggleViewAction();
    m_impl->propertiesToggleAction->setText("&Properties");
    m_impl->propertiesToggleAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
    m_impl->propertiesToggleAction->setStatusTip("Toggle the slide properties panel");
```

Immediately after that block, add:

```cpp
    m_impl->metadataToggleAction = m_impl->metadataPanel->toggleViewAction();
    m_impl->metadataToggleAction->setText("Meta&data");
    m_impl->metadataToggleAction->setShortcut(QKeySequence("Ctrl+Shift+D"));
    m_impl->metadataToggleAction->setStatusTip("Toggle the slide/scene metadata panel");
```

(`Ctrl+Shift+D` is unused in the View menu; the chosen `&d` mnemonic in "Meta**d**ata" doesn't conflict with existing mnemonics — `&Channels`, `&Scenes`, `&Associated Images`, `&Properties`, `&Minimap`.)

- [ ] **Step 5: Add the action to the View menu**

Find `void createMenus()` inside `Impl` and locate the line:

```cpp
        viewMenu->addAction(propertiesToggleAction);
```

Immediately after that line, add:

```cpp
        viewMenu->addAction(metadataToggleAction);
```

- [ ] **Step 6: Populate the panel on `slideOpened`**

In `connectSignals()`, find the existing `slideOpened` lambda and the line:

```cpp
                propertiesPanel->setSlideInfo(info);
```

Immediately after that line (still inside the lambda), add:

```cpp
                metadataPanel->setSlideInfo(info);
```

- [ ] **Step 7: Clear the panel on `slideClosed`**

In `connectSignals()`, find the existing `slideClosed` lambda and the line:

```cpp
            propertiesPanel->clear();
```

Immediately after that line (still inside the lambda), add:

```cpp
            metadataPanel->clear();
```

- [ ] **Step 8: Build**

```
cmake --build build/build --config Release
```

Expected: build succeeds. The new `Metadata` menu item is now in View, the panel exists but is hidden until toggled.

- [ ] **Step 9: Commit**

```bash
git add src/ui/src/MainWindow.cpp
git commit -m "Wire MetadataPanel into MainWindow (dock, action, signals)"
```

---

## Task 8: End-to-end smoke test

This is the verification step that replaces unit tests for this feature, matching the precedent set by the existing UI panels (per the spec, section 5).

**Files:** none modified.

- [ ] **Step 1: Launch the viewer**

Run:

```
build/build/Release/slideio-viewer.exe
```

Expected: application starts.

- [ ] **Step 2: Verify the View menu entry**

Open the **View** menu. Confirm the **Meta&data** item is present (with mnemonic `d` underlined) and shows the `Ctrl+Shift+D` shortcut. Click it: the **Metadata** dock pane appears on the right side, currently empty.

- [ ] **Step 3: Open a slide and inspect the tree**

Use **File → Open Slide…** to open a slide whose driver is known to expose rich metadata (e.g., a CZI, NDPI, or SVS file). After the slide loads:

1. The dock pane shows two top-level items: **Slide** and **Scene**, both expanded.
2. Each item's value column shows either `{N keys}` / `[N items]` (real metadata) or `(no metadata)` (driver returns `Null`).
3. Expanding a node reveals nested keys (objects) or indexed items (`[0]`, `[1]`, … for arrays).
4. Scalar leaves show the value formatted as expected: `true`/`false` for booleans, plain integers, `%.15g`-formatted doubles (no trailing zeros for whole numbers), strings unquoted.

- [ ] **Step 4: Switch scenes and confirm the Scene subtree updates**

If the slide is multi-scene, open the Scenes panel (View → Scenes), click a different scene thumbnail. The Scene subtree in the Metadata pane refreshes to the new scene's metadata; the Slide subtree stays the same (slide-level metadata is shared).

- [ ] **Step 5: Test "Copy metadata as text"**

Right-click anywhere in the tree → **Copy metadata as text**. Paste into a text editor. Expected output: an indented `Property: Value` listing with two-space indentation per level, regardless of which nodes were expanded in the UI.

- [ ] **Step 6: Test "(no metadata)" rendering**

Open a slide whose driver does not expose parsed metadata (or, if all locally available test slides have metadata, accept the `Null` path is exercised by the unit-test-style branch in `addRootNode` — verified by code reading). The corresponding parent ("Slide" or "Scene") should display `(no metadata)` in the Value column with no expandable children.

- [ ] **Step 7: Test layout persistence**

Drag the Metadata dock to the left side of the main window. Close the application. Re-launch. The Metadata dock should reappear visible, on the left side, exactly where you left it. Toggle it off, close, re-launch — it should reappear hidden.

- [ ] **Step 8: Test slide close clears the pane**

With a slide open and the Metadata pane visible, **File → Close**. The Metadata pane should empty out (both "Slide" and "Scene" parents disappear) without errors.

- [ ] **Step 9: If any step fails, debug and amend the relevant earlier task**

Each smoke-test failure points to a specific task: tree population issues → Task 5; copy-as-text issues → Task 6; menu/dock/signals issues → Task 7; missing/wrong values → Task 3 (the `convertMetadata` helper).

- [ ] **Step 10: Final commit (only if any fixes were needed)**

If smoke testing surfaced and required code changes:

```bash
git add -p   # review each fix
git commit -m "Fix metadata pane <issue> found during smoke test"
```

If the implementation passed all steps cleanly, no extra commit is needed.

---

## Final state

After completing all tasks:

```
docs/superpowers/specs/2026-05-10-metadata-dock-pane-design.md   (already committed)
docs/superpowers/plans/2026-05-10-metadata-dock-pane.md          (this plan)

src/core/include/slideio/viewer/core/Metadata.h                  (new)
src/core/include/slideio/viewer/core/Types.h                     (modified)

src/infra/src/SlideIOAdapter.cpp                                 (modified)

src/ui/include/slideio/viewer/ui/MetadataPanel.h                 (new)
src/ui/src/MetadataPanel.cpp                                     (new)
src/ui/src/MainWindow.cpp                                        (modified)
src/ui/CMakeLists.txt                                            (modified)
```

Seven commits along the way (one per task except Task 8, which only commits if fixes were needed).
