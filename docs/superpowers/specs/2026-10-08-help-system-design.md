# Help system — design

**Date:** 2026-10-08
**Status:** Proposed
**Base:** `main` @ `4641e53`

## Goal

Give the viewer the help it promises but does not have. The Help menu today holds
one entry — About.

`02-user-interface-design.md` §2 specifies a User Guide on F1 and a Keyboard
Shortcuts entry on Ctrl+/; §7.5 specifies tooltips carrying shortcuts and
first-run coach marks; `FR-UI-09` makes the onboarding a *Should*. None of it
exists.

The audience is pathologists and clinical users evaluating or reading slides. That
decides the emphasis. A clinical user's confusion is rarely "where is the manual" —
it is **"why is that greyed out"** and **"what is this telling me"**, in the moment,
without leaving the slide. Open a fluorescence CZI today and Color Management goes
grey with no explanation anywhere in the application. Nobody opens a manual to
resolve a greyed-out menu item.

So the design puts the contextual layer first and the prose second, and treats
colour management as the anchor content rather than one topic among many.

## Non-goals

- **This is not the Instructions for Use.** It is a usability aid. The device's IFU
  under EU MDR, if and when one exists, is a separate controlled document owned
  elsewhere. One consequence is called out in §4.4 and §10.
- **No coach marks in this round.** `FR-UI-09` is a *Should*, it is the most code
  for the least durable value, and two of its three scripted hints describe
  annotation tools that do not exist until Stage 3. Revisit after Stage 3.
- **No full-text search.** The topic set is small enough to navigate by tree. Search
  is worth adding when the content outgrows one screen of topics, not before.
- **No hosted documentation site.** Rejected in favour of in-application help; see
  §1.1.
- **No printing, export, or localisation.**
- **No content for annotations, cases, bookmarks or split view.** They do not exist.
  The guide documents what ships today and grows per feature.

## 1. Approach

### 1.1 Why in-application rather than hosted

A hosted docs site opened with `QDesktopServices::openUrl` would be cheaper to build
— the call is already there for the log file at `MainWindow.cpp:329` — and content
could change without a release.

Rejected on two counts. Clinical workstations are routinely locked down without
internet access, so the help would be absent exactly where it is needed. And hosted
content drifts from the installed build with nothing tying the two together; for a
feature where the user is deciding whether a colour is trustworthy, reading the
wrong version's description is worse than reading none.

Compiling the content into the binary makes the help offline by construction and
pins it to the build that shipped it.

### 1.2 Why not Qt Assistant

`.qch` means deploying a second executable, a collection-file build step, and a
window that will not follow this application's dark theme. The `windeployqt` setup
in `CMakeLists.txt` is already intricate. `QTextBrowser` is in `Qt6::Widgets`, which
`slideio-viewer-ui` already links — no new dependency, no new binary, nothing new to
install.

### 1.3 Three increments

| # | What | Why it earns its place |
|---|---|---|
| 1 | Explain disabled actions | Highest clinical value; extends a pattern already in the code |
| 2 | Keyboard shortcuts, generated | Self-maintaining; 16 shortcuts are visible nowhere as a set |
| 3 | The guide, colour management first | Hardest content proves the machinery |

These are identifiers, not a delivery order — increment 2 is a page inside increment
3's window and cannot ship before it. §9 gives the actual sequence.

## 2. Layering

Help is a presentation concern with no domain logic. **Nothing goes in `core`.**

The repo has a settled pattern for testable UI logic: `AboutInfo`, `DriverFilters`,
`ColorProfilePolicy` and `SlideProfilesDialog` all expose pure free functions over
plain data, with widgets built on top, and the pure part tested in
`slideio-viewer-ui-tests`. Help follows it.

**The constraint that enforces it:** `slideio-viewer-ui-tests` creates no
`QApplication`. `tests/CMakeLists.txt:38` links `slideio-viewer-ui` and
`Catch2::Catch2WithMain` and nothing else, and every existing test there operates on
plain structs — `buildSlideProfileRow` takes a `ColorProfileOverride` and returns a
`SlideProfileRow` without touching a widget.

Therefore **no tested function may take a `QAction*`, `QMenu*` or `QMenuBar*`.**
Widget traversal is a thin untested collector; everything that can be *wrong* —
formatting, grouping, selection rules, rendering — is pure and tested.

### New files

| File | Purpose | Tested |
|---|---|---|
| `src/ui/include/slideio/viewer/ui/ActionReason.h` / `src/ui/src/ActionReason.cpp` | The disabled-reason rule | Pure part |
| `src/ui/include/slideio/viewer/ui/ShortcutSheet.h` / `src/ui/src/ShortcutSheet.cpp` | Shortcut rows and Markdown rendering | Pure part |
| `src/ui/include/slideio/viewer/ui/HelpTopics.h` / `src/ui/src/HelpTopics.cpp` | Topic registry, link resolution | Yes |
| `src/ui/include/slideio/viewer/ui/HelpWindow.h` / `src/ui/src/HelpWindow.cpp` | The window | Hand-verified |
| `resources/help.qrc`, `resources/help/*.md` | Content | Resolution tested |
| `tests/ui/ActionReasonTest.cpp`, `ShortcutSheetTest.cpp`, `HelpTopicsTest.cpp` | — | — |

## 3. Increment 1 — explaining disabled state

### 3.1 The rule already exists

`MainWindow.cpp:508-530` works out the rule, with the reasoning in its comments:

> The two tips carry different things on purpose. The tooltip renders in the menu
> (viewMenu has setToolTipsVisible) and is where a problem belongs; the status tip is
> the action's description, which a broken override on an otherwise fine slide must
> not cost the user. Only when colour management is unavailable — when there is no
> behaviour left to describe — does the status tip give up the description for the
> reason.

`viewMenu->setToolTipsVisible(true)` at `MainWindow.cpp:252` makes the tooltip render
inside the menu, which is what lets a *disabled* item explain itself at all.

The rule is correct. It is hand-written at two call sites and covers colour
management only.

### 3.2 What is unexplained

Five actions can be disabled. Three explain themselves; two do not:

| Action | Disabled when | Explains itself |
|---|---|---|
| `colorManagementAction` | Unavailable for the slide | Yes |
| `setSlideProfileAction` | Slide is not colorimetric | Yes |
| `clearSlideProfileAction` | No override stored for this slide | **No** |
| `clearDefaultProfileAction` | No default profile set | **No** |
| `closeAction` | No slide open | **No**, and the File menu has no tooltips enabled |

### 3.3 The change

```cpp
// ActionReason.h
namespace slideio::viewer::ui
{

/// The status tip keeps the action's description unless the action is disabled and
/// a reason is available -- at which point there is no behaviour left to describe,
/// so the reason takes the status bar.
std::string chooseStatusTip(const std::string& description,
                            const std::string& reason,
                            bool enabled);

/// Sets the action's enabled state and its explanation together.
void setActionReason(QAction* action,
                     const std::string& description,
                     const std::string& reason,
                     bool enabled);

} // namespace slideio::viewer::ui
```

`setActionReason` sets the enabled state *itself* rather than taking an
already-configured action. That is deliberate: it makes it impossible to disable an
action without supplying a reason, and impossible for a reason to desynchronise from
the state it explains. An empty `reason` clears the tooltip.

This generalises a trap the project already paid for. `MainWindow.cpp:607-611` records
it:

> The action is disabled because no slide is open now, not because of whatever slide
> the tooltip/status tip last described — clear that reason rather than let it outlive
> the slide it was about.

One helper with one clearing discipline is harder to get wrong than five hand-written
pairs.

### 3.4 Scope of increment 1

- `ActionReason.h/.cpp` with `chooseStatusTip` tested.
- The two existing colour-management call sites converted to it, behaviour unchanged.
- `clearSlideProfileAction`, `clearDefaultProfileAction` and `closeAction` given
  reasons.
- `setToolTipsVisible(true)` on the File menu.

No new window, no content, no resources. It ships on its own.

## 4. Increment 3 — the guide

Presented before increment 2 because increment 2's output is a page *inside* it.

### 4.1 Rendering

The window reads a topic's resource and calls `QTextBrowser::setMarkdown()` itself,
with `setOpenLinks(false)` and navigation handled through `anchorClicked`.

This is the decision that removes the one technical unknown. We never rely on
`QTextBrowser` inferring a source type from an extension, and never rely on it
resolving relative resource URLs or generating heading anchors from Markdown. Every
render goes through one function, which also makes the Markdown-versus-HTML choice
cheap to reverse later.

Links:

- A relative `*.md` link resolves against the topic registry and loads that topic.
  Relative rather than a custom `topic:` scheme so the content still reads correctly
  as Markdown in a pull request.
- An `http`/`https` link goes to `QDesktopServices::openUrl`, matching the log-file
  handling at `MainWindow.cpp:329`.
- Anything unresolvable shows the fallback page (§7).

**Known limitation.** `QTextDocument::setDefaultStyleSheet` applies to HTML import,
not Markdown, so typographic control over imported Markdown is weak. Background,
foreground and link colour come from the widget stylesheet, which is sufficient. If
styled code blocks or callouts are ever wanted, the single render function switches
to HTML; that is the escape hatch and it is one function wide.

### 4.2 The window

`HelpWindow`, a non-modal `QDialog` with PIMPL per the convention in
`phase4-implementation-strategy.md`. A `QSplitter` holding a `QTreeWidget` of topics
on the left and a `QTextBrowser` on the right.

**One instance.** Opening help while it is already open raises the existing window and
switches topic rather than stacking windows.

No back/forward history: the topic tree is always visible, so returning is one click.

Dark theme via a `kDialogStyle` constant following `AboutDialog` and
`SlideProfilesDialog.cpp:27`, extended for `QTextBrowser` and `QTreeWidget`.

### 4.3 Topic registry

```cpp
// HelpTopics.h
enum class HelpTopic
{
    OpeningSlides,
    Navigating,
    ScenesAndAssociatedImages,
    Channels,
    ZStacksAndTimeSeries,
    ColorProfiles,
    ColorUnavailable,
    SlideProfile,
    DefaultProfile,
    SlideProperties,
    Shortcuts,
    Troubleshooting,
};

struct HelpTopicInfo
{
    HelpTopic topic;
    const char* fileName;      ///< "color-profiles.md", also the link target
    const char* title;         ///< tree label
    const char* resourcePath;  ///< ":/help/color-profiles.md"
};

const std::vector<HelpTopicInfo>& helpTopics();
const HelpTopicInfo* findHelpTopic(const std::string& fileName);
const HelpTopicInfo* findHelpTopic(HelpTopic topic);
```

### 4.4 Colour management — the anchor content

Four of the twelve topics. It has the most states, the most places to set it, and it
is the only feature where a misunderstanding changes what the user believes about a
slide.

| Topic | Answers |
|---|---|
| `ColorProfiles` | What this does, what the status bar's `sRGB` means, what it does not do |
| `ColorUnavailable` | The three greyed-out reasons |
| `SlideProfile` | Overrides, displacing an embedded profile, why setting one reopens the slide |
| `DefaultProfile` | Why it affects only slides that embed nothing, and only newly opened ones |

**The precedence table** is the single most valuable paragraph in the help, and it
appears nowhere in the UI:

| Source | Applies when | Shown in Properties as |
|---|---|---|
| Per-slide override | Always, if set for this slide — displaces the embedded one | `Per-slide override` |
| Embedded in the slide | No override is set for this slide | `Slide` |
| Default profile | The slide embeds nothing **and** no override is set | `Default setting` |

Every constructible user misunderstanding is a misreading of that table. *"I set a
default and nothing changed"* — the slide embeds its own. *"Still nothing changed"* —
it applies to slides opened from now on. *"Which one am I looking at?"* — the Origin
row says so, for a reader who knows to look.

**Content rule: quote the UI exactly.** `colorManagementUnavailableReason`
(`ColorManagement.cpp:31-46`) produces exactly three sentences. `ColorUnavailable`
reproduces them verbatim as its headings:

- "Color management applies to RGB brightfield slides only"
- "This slide embeds no ICC profile, and no default profile is set"
- "This slide's color profile could not be used"

A user searching the help for the tooltip they are staring at must land on that
heading, which only works if the words match. The same rule governs
`colorProfileOriginName` (`SlidePropertiesPanel.cpp:66-74`), the `displaced: <name>`
note at `:81-88`, and the fact that `originRowLabel` reads **"Origin when managed"**
while colour management is off — meaning *this is what would apply* — which the
`SlideProperties` topic must explain.

**The paragraph that needs care.** `ColorProfiles` must answer "can I trust this
colour", and the honest answer is narrow: colour management converts the slide's
recorded colour space to sRGB for display; it does not calibrate the monitor, and it
cannot correct a slide whose embedded profile is wrong.

This help is scoped as a usability aid, not the IFU. That one paragraph is
nonetheless where a usability document can drift into a device performance claim
without anyone intending it. It is written factually as above, and **that page gets a
Regulatory read before release.** This does not reclassify the system; it is one page.

### 4.5 Remaining topics

`OpeningSlides` (including the DICOM folder path and why it forces the `DCM` driver)
· `Navigating` (zoom, pan, fit, actual pixels, minimap — including mouse and
trackpad) · `ScenesAndAssociatedImages` · `Channels` (brightness/contrast,
fluorescence) · `ZStacksAndTimeSeries` · `SlideProperties` · `Troubleshooting` (the
log file, unreliable levels).

### 4.6 Context map

F1 and dialog Help buttons open the window on a specific topic rather than a table of
contents.

| From | Topic |
|---|---|
| `SlideProfilesDialog` Help button | `SlideProfile` |
| Slide properties panel, F1 | `SlideProperties` |
| The displace-warning dialog's help link | `SlideProfile` |
| Channel mixer panel, F1 | `Channels` |
| Scene thumbnail / associated images panels, F1 | `ScenesAndAssociatedImages` |
| Main window, F1, nothing focused | `OpeningSlides` |
| Help ▸ Keyboard Shortcuts, Ctrl+/ | `Shortcuts` |

`SlideProfilesDialog` is the strongest case: it displays statuses — "File missing",
"Not a profile", "Not RGB" — that are explained nowhere.

The displace warning asks a real clinical question in one sentence —
*"Overriding it displays the slide through a profile the scanner did not produce.
Continue?"* (`MainWindow.cpp:1179-1181`) — and gains a link rather than a longer
sentence.

The greyed-out Color Management item keeps its tooltip; a tooltip cannot carry a
link, so the depth lives in `ColorUnavailable` under the identical heading.

## 5. Increment 2 — keyboard shortcuts

A page inside the help window, not a second dialog. One window, one mental model.

### 5.1 Generated, not authored

The 16 shortcuts are defined in two separate places: `MainWindow.cpp:146-199`, and
again at `:905-921` for the five dock toggles. A hand-written list is precisely the
kind that captures the first block and misses the second.

### 5.2 Split for testability

```cpp
// ShortcutSheet.h
struct ShortcutEntry
{
    std::string group;   ///< menu title, accelerators stripped
    std::string label;   ///< action text, accelerators stripped
    std::string keys;    ///< QKeySequence::NativeText
};

std::string stripAccelerators(const std::string& text);
std::string renderShortcutTableMarkdown(const std::vector<ShortcutEntry>& entries);

/// Thin widget traversal. Untested by construction -- it needs a QApplication.
std::vector<ShortcutEntry> collectShortcuts(const QMenuBar* menuBar);
```

`collectShortcuts` walks top-level menus, recurses into submenus, and emits an entry
for each action with a non-empty `shortcut()`, taking `group` from the top-level menu
title. Separators and actions without shortcuts are skipped, which excludes the
recent-files entries naturally.

The pure functions carry everything that can be wrong. The walk is four lines of Qt.

### 5.3 The page

The `Shortcuts` topic is authored Markdown **plus** generated Markdown, concatenated
into one string and passed to the single render function:

1. An authored preamble covering **mouse and trackpad** — scroll to zoom, middle-click
   to pan. These are viewport handlers, not `QAction`s, so the walk cannot see them,
   and they are the two a pathologist needs most. The generated table covering only
   menu actions would silently omit the most important rows.
2. The generated table, as a GitHub-style Markdown table, which `setMarkdown`
   supports.

## 6. Resources and the build

`resources/help.qrc`, prefix `/help`, listing `help/*.md` and any `help/images/*.png`.

`CMAKE_AUTORCC` is already `ON` (`CMakeLists.txt:61`), and `resources/icons.qrc` is
listed directly in `_executable_sources` (`:75,79`). `help.qrc` follows that
precedent exactly — one line.

**It is also added to the `slideio-viewer-ui-tests` sources**, so `HelpTopicsTest` can
assert that every registered topic resolves. AUTORCC generates a separate registration
per target; the duplication is small and it buys the only test that catches the
realistic mistake. Resource registration is a static initialiser and needs no
`QApplication`, so this works in a test binary that has none.

Screenshots are supported by this layout (`QTextBrowser` renders images from qrc, and
`docs/images/viewer.png` sets a precedent for keeping them in-repo) but none are
required for the first content set.

## 7. Error handling

| Case | Behaviour |
|---|---|
| Topic's resource missing or unreadable | A fallback page built in code, not loaded from a resource, stating the topic is unavailable. The tree stays usable. |
| Link matching no registered topic | The same fallback page. |
| External link fails to open | `QMessageBox::warning`, matching the log-file handling at `MainWindow.cpp:330`. |
| Help requested while the window is open | Raise and switch topic. Never a second window. |

A compiled-in resource cannot realistically go missing, but a registry entry pointing
at a renamed file can. §8 catches that before it ships.

## 8. Testing

All three test files live in `tests/ui/` and are added to `slideio-viewer-ui-tests`.
None constructs a widget.

**`ActionReasonTest.cpp`**
- An enabled action keeps its description regardless of reason.
- A disabled action with a reason surfaces the reason.
- A disabled action with an empty reason keeps its description.

**`ShortcutSheetTest.cpp`**
- `stripAccelerators` removes `&` and preserves a literal `&&`.
- `renderShortcutTableMarkdown` groups by menu, preserves order within a group, and
  emits a well-formed table.
- An empty entry list renders no table rather than an empty one.

**`HelpTopicsTest.cpp`**
- Every `HelpTopic` enum value appears exactly once in `helpTopics()`.
- Every registered `resourcePath` exists — `QFile::exists(":/help/...")`.
- Every title and file name is non-empty; file names are unique.
- **Every relative `*.md` link appearing in the content resolves to a registered
  topic.** This catches broken cross-links, which are otherwise invisible until a user
  follows one.

Window behaviour, F1 routing and theming are hand-verified, consistent with how
dialogs are handled in this repo (`SlideProfilesDialog`, `AboutDialog`).

## 9. Sequencing

1. **Increment 1** — `ActionReason`, the five actions, File menu tooltips. No
   resources, no window. Independently shippable.
2. **Increment 3a** — `HelpTopics`, `HelpWindow`, `help.qrc`, the four colour
   management topics, the Help menu entry, F1 wiring and the `SlideProfilesDialog`
   Help button. The hardest content proves the machinery.
3. **Increment 2** — `ShortcutSheet` and the `Shortcuts` topic, which needs the window
   from 3a.
4. **Increment 3b** — the remaining seven topics, which can land incrementally and
   alongside the features they describe.

Coach marks are not scheduled; revisit after Stage 3.

## 10. Flags

- **The `ColorProfiles` "can I trust this colour" paragraph needs a Regulatory read**
  before release (§4.4). One page, not a reclassification.
- **The guide is deliberately early for most of the app.** Stage 3 adds annotations
  and Stage 4 adds cases, bookmarks and split view. Writing their prose now means
  writing it twice. Increment 3b is therefore open-ended by design, and the shortcut
  page — which generates itself — is the part that cannot go stale.
- **`02-user-interface-design.md` §7.5 specifies toolbar tooltips.** There is no
  toolbar; `QToolBar` appears nowhere in `src/`. That requirement is unactionable until
  one exists and is not addressed here.
- **The status bar is a contended channel.** The 26 `setStatusTip` calls are delivered
  by Qt through `QStatusBar::showMessage`, the same channel as
  `StatusBarManager::showTransientMessage`. Hovering a menu item clears a transient
  acknowledgement early. Not a defect, and not changed here, but increment 1 increases
  the traffic on that channel and a future design should know it.
