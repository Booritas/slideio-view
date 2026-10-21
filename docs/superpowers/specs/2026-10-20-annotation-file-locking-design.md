# Advisory locking on annotation files — design

Status: **implemented 2026-10-20**, group AE not yet run. Closes FR-ANN-32
("File locking (advisory) shall be used on annotation files to prevent data loss
from concurrent access by multiple processes", Must).

The project owner opted out of the design questions for this round, so the
decisions below are mine unless marked otherwise.

## 1. Why

Until today, two instances of the viewer with the same slide open were
last-writer-wins, silently. Both load the same file, both autosave every two
seconds, and the second write erases whatever the first one added — with no
error, no dialog and nothing in the log. The user discovers it when annotations
they remember drawing are not there.

Two instances is not a contrived state. A workspace folder can sit on a share;
one person can run two copies; a slide can be open on a workstation and on a
laptop at once.

## 2. Goals, non-goals, anti-goals

**Goals.** When this application is already editing a (slide, scene)'s
annotations, a second instance opening the same pair shows them and refuses to
write them, and says why in words naming who holds it.

**Non-goals.** Protection against anything that is not this application: a text
editor, a sync client, a backup agent. Mandatory locking. Merging concurrent
edits. A lock on the slide file itself. Waiting for a lock to become free.

**Anti-goals** — ways this fails even if it works:

- **Stealing a live colleague's lock.** The failure this exists to prevent,
  reintroduced by a stale-detection rule that is too eager.
- **Refusing to work where no lock can be taken.** Advisory means cooperative.
  A workspace that cannot hold a lock file is exactly the state this application
  ran in until today, and turning that into "you may not annotate" would be a
  regression wearing a safety feature's clothes.
- **Refusing to SHOW annotations somebody else holds.** A reader opening a slide
  a colleague is annotating should see what is there.
- **A lock nothing releases.** Every path out of an open scene has to give it
  up, including a crash, a workspace switch and a slide that turned out to be
  unreadable.

## 3. What it is

`QLockFile` on `<annotations>/<slideId>.s<N>.annotations.json.lock` — Qt's own
convention of the guarded file's name plus `.lock`, so the two sort together and
anyone looking in the folder can see what is held. Qt writes the process id,
the host name and the application name into it, which is where the "held by"
message comes from.

**Keyed by (slide, scene), like the annotation file itself.** A lock on the
slide alone would lock a colleague out of every other scene of a multi-scene
slide, which have their own coordinate spaces and their own files.

**Taken in `beginSlide`, before the load**, because the lock is about the key
and not about what turned out to be in the file, and taking it first narrows the
window in which two instances both believe they may write.

**Held exactly while the service may write.** `setActive(false)` releases, which
makes the invariant structural rather than something each of the half-dozen
sites that turn writing off has to remember. So a slide that loaded read-only
for any other reason — a malformed file, a slide-id mismatch resolved as
read-only — gives the lock straight back, and does not lock out another instance
that might repair it.

**Contention is not a load failure.** The annotations are loaded and shown; only
`isActive()` goes false. That reuses the whole existing read-only path: drawing
tools disabled, the counting combo disabled, the tally and the new counts export
still working, and the persistent "annotations blocked" indicator in the status
bar.

### The decisions, and why

- **`QLockFile`, not a hand-rolled lock file.** It already does the hard part:
  atomic creation, the owner's pid and host, and stale detection that can ask
  the operating system whether a local pid is still alive.
- **Stale time of four hours.** Qt treats a lock as abandoned either when its
  process is gone — which it can check directly when the lock names *this* host,
  immediately, whatever this value is — or when the file is older than this. The
  second rule is the only answer available for a lock taken by another machine
  over a share, where no process can be interrogated. Four hours is long enough
  that it cannot expire during a reading session and short enough that a remote
  crash does not block the slide for a working day. **The recovery for a lock
  left by a crashed remote instance is to delete the `.lock` file**, which is
  what advisory locking costs.
- **`tryLock(0)`, never blocking.** This runs on the UI thread during a slide
  open. A blocking acquire would freeze the application for as long as somebody
  else kept the slide open.
- **`Unavailable` is not a refusal.** A folder that cannot hold a lock file will
  refuse the save too, and that path already reports itself.
- **The annotations folder is now created on the first lock**, not only on the
  first save. A lock file cannot live in a folder that does not exist, and the
  scene most worth locking is the one nobody has annotated yet — where two
  instances both start from empty and the second erases the first. The cost: an
  installation that opens a slide and never annotates leaves an empty
  `annotations/` folder behind. Recorded here because the repository's header
  used to promise the opposite.
- **A re-root releases.** `setWorkspaceRoot` unlocks, because the lock names a
  path under the old root. `WorkspaceSwitch::run` calls `endSlide`, repoints and
  calls `beginSlide`, so the lock is re-acquired under the new root by the
  ordinary path.
- **The holder string says only what it learned.** Every field of a lock file on
  a share can be unreadable at the moment it is asked about, so an unidentified
  holder yields an empty string and the message drops the parenthesis rather
  than inventing a name.
- **Logged, not shown as a dialog.** The read-only reason already tells the user,
  and a modal on top of it would interrupt a reader who is allowed to read. The
  `spdlog::warn` line is for the support question an hour later, which a
  transient message cannot answer.

## 4. Where the code lives

- `core::LockStatus`, `core::LockResult`, and `lock()`/`unlock()` on
  `core::IAnnotationRepository` (`AnnotationRepository.h`). No Qt, so `app`
  never sees `QLockFile`.
- `infra::JsonAnnotationRepository::lock/unlock` — the `QLockFile`, the stale
  time, the holder string. **10 tests**, driven by two repositories over one
  temporary directory: `QLockFile` cannot tell a second lock in this process
  from one in another, which is exactly what makes two instances testable at
  all.
- `app::AnnotationPersistenceService` — acquires, decides, releases, and exposes
  `isLockedByAnother()` and `lockHolder()`. **9 tests**, against a fake
  repository that returns whichever `LockStatus` the test wants.
- `ui::ViewportWidget` composes the read-only reason, choosing between "another
  window has it" and "the stored file could not be used". The user can act on
  the first and not the second, and reporting the second for the first would
  send somebody looking for a corrupt file that is perfectly fine.

## 5. What this does not protect

Advisory means cooperative. It stops two copies of **this** application
overwriting each other and stops nothing else: not a text editor, not a sync
client, not a backup agent, and not a filesystem that reports locks it does not
honour. A workspace on a share whose locking is unreliable degrades to the old
last-writer-wins behaviour rather than to an error.

The window is narrowed, not closed: two instances that open the same scene in
the same instant can both believe they took the lock, because the check and the
decision are not one atomic operation across machines.

## 6. Manual checks — group AE

Two running copies of the viewer are needed; none of this is reachable from a
suite. Written before the run.

| | Check | Result |
|---|---|---|
| AE1 | Open a slide in instance A, then the same slide and scene in instance B: B shows the annotations and the drawing tools are disabled. | Pass — 27 shown, all six tool actions disabled |
| AE2 | B's status bar says another window has them open, and names the holder. | Pass, on the "Annotations disabled" tooltip — see below |
| AE3 | The log in B carries a line naming the lock file and the holder. | Pass |
| AE4 | A `.lock` file exists beside the annotation file while A has it open. | Pass |
| AE5 | Close the slide in A: the `.lock` file disappears. | Pass |
| AE6 | Reopen the slide in B after A closed it: B can annotate, and the lock is now B's. | Pass — the lock file names B's pid |
| AE7 | Annotate in B while it is locked out — the tools are disabled, so confirm nothing can be drawn and the file on disk is unchanged. | Pass — M, R and P then clicks: nothing drawn, file hash unchanged |
| AE8 | Open scene 0 in A and scene 1 of the same slide in B: both can annotate. | Pass — each saved a marker under its own `.s0`/`.s1` lock |
| AE9 | Kill instance A from Task Manager, then open the slide in B: the lock is recognised as dead and B can annotate. | Pass — killed by `TerminateProcess`, the `.lock` left on disk, B took it over |
| AE10 | Open a slide in A that has never been annotated: an `annotations/` folder appears with a `.lock` in it and no `.json`. | Pass |
| AE11 | With A holding a lock, change B's workspace in Preferences to a different folder: B can annotate there. | Pass |
| AE12 | With A holding a lock, change A's workspace: the old root's `.lock` is gone and a new one exists under the new root. | Pass |
| AE13 | A slide whose annotation file is malformed still reports "the stored file could not be used", not the lock message, and leaves no `.lock` behind. | Pass |
| AE14 | An associated image takes no lock. | Pass |
| AE15 | Export Counts works in B while it is locked out. | Pass |
| AE16 | Delete a stale `.lock` by hand while B has the slide open read-only, reopen the slide in B: B can annotate. | Pass, with a planted remote-host lock — see below |

**Run 2026-10-21 against the installed build (sha256 `f2aa48f1…`, commit
`016dfd5`): 16/16 passed.** Driven by UI Automation and synthetic input, every
lock state read from the `.lock` file's own contents (pid, application, host)
rather than inferred from the UI. Fixtures: Philips-1 (scene 0, 27 annotations),
the three-scene `scn/ultivue/Leica Aperio Versa 5 channel fluorescent image.scn`
and the never-annotated `svs/CMU-1-Small-Region.svs`; the Philips file was
restored from a byte-identical backup afterwards.

- **AE2.** The reason reads "Another window has this slide's annotations open
  (slideio-viewer on DESKTOP (process 9416)), so they are read-only here." It is
  on the tooltip of the persistent "Annotations disabled" label; the transient
  message had expired by the time it was read. Cosmetic: the holder string
  carries its own parentheses, so the message nests them.
- **AE16** needs a lock that is stale but not detectably dead, and a lock this
  host took is detected dead immediately (that is AE9). So the lock was planted
  by hand naming another host — pid 4242 on `READING-ROOM-2` — which is the real
  case: a remote instance that crashed. B opened read-only naming that holder;
  deleting the file and reopening gave B the lock and its tools back.
- **AE11 and AE12** each went through the "Change annotation folder" dialog. For
  the locked-out B it offered only *Switch without copying*, and said the
  annotations on screen were read-only and would not be carried over; for A it
  also offered *Copy to the new folder*, which was taken.
- **AE14.** The Label image opened in its own window; the only `.lock` files
  before and after were the slide's own and another instance's.

**One finding, not one of the sixteen checks.** Preferences shows the workspace
from the settings **every instance shares**, not the root this instance is using
(`AnnotationSettings::workspaceDirectory` reads `QSettings` on each call). After
B switched to `ae-ws2`, A's Preferences opened showing `D:/tmp/ae-ws2` while A
was still rooted at `D:/tmp/ad-run`. Inferred from `MainWindow.cpp:1661-1663`
and not exercised: pressing OK there without touching the field would switch A
into B's folder, where B holds the lock. The Export Counts dialog starts from the
same setting, so it would open in the other instance's folder too. Two instances
are exactly what locking exists for, so this is the configuration in which it
shows.

## 7. Requirements

FR-ANN-32 (Must) — advisory file locking on annotation files. NFR-REL-04, the
write-ahead log and crash recovery, is **not** covered by this and remains open:
up to one autosave interval is still lost on a hard kill.
