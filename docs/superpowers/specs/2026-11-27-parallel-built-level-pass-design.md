# Parallel built-level pass — design

Date: 2026-11-27. Status: proposed. Branch: `feature/parallel-built-level-pass`
(cut from `feature/slideio-gdal-pyramids`, which pins SlideIO at `43d1d380`).

## 1. Why

The background pass that builds a large slide's resident levels
(`infra::BuiltPyramidSource::run`, spec `2026-11-27-built-pyramid-levels-design.md`
§5.3) reads the source level in 4 MiB chunks **one at a time** on one worker
thread. For `PIT_01_00248_01.tiff` (63803×44997, 22 bands × 125 chunks) it takes
**60.3 s** (smoke run 2026-11-27 on the new pin) — almost all of it decoding.
Until SlideIO PR #104 a GDAL scene serialised every read, so a second reader
would have waited anyway. Since #104 GDAL scenes read concurrently, so several
chunks can decode at once. Goal: the same build in a fraction of the time, with
byte-identical results.

## 2. Goals, non-goals, anti-goals

Goals
- Read source chunks with several threads; the built levels are **byte for byte
  identical** to the sequential build (same chunks, same order into the
  accumulator, same failures).
- Bounded memory: at most a small, fixed number of chunks decoded but not yet
  accumulated.
- Close and colour toggle still cancel promptly: the join waits for at most the
  reads in flight, each one chunk.

Non-goals
- Changing the accumulator, the band/chunk geometry, the levels planned, the
  overview rules, the disk cache or the stand-ins.
- Parallelising the averaging itself (cheap next to decoding).
- Thread priorities.

Anti-goals
- Starving the view: the user's visible tiles must still load while a build
  runs (the tile scheduler has its own `max(2, hw − 2)` workers).
- Any difference in what is drawn, cached or saved compared with today.
- New failure modes: an unreliable source level, a failed chunk, a cancel and
  an exception must behave exactly as they do now.

## 3. Approaches

- **A. Several readers, one in-order feeder (chosen).** `run()` keeps the one
  accumulator. A small pool of reader threads claims chunk indices in band-major
  order from a shared counter, reads them unlocked, and parks the result in a
  reorder slot; `run()` feeds slots to the accumulator strictly in chunk order,
  exactly as today, and notifies after each band. Memory is bounded by a window:
  a reader may not claim chunk *k* until chunk *k − window* has been fed.
  Identical output by construction; the accumulator and every rule downstream
  are untouched.
- **B. Horizontal stripes, one accumulator each, merged.** Each thread builds
  its own stripe of the slide. Needs stripes aligned to the coarsest resident
  factor, per-stripe accumulators, a merge, and a rethink of "rows ready" for
  stand-ins and notifications. More code, more memory, a new merge to get
  wrong — rejected.
- **C. Parallelise only the averaging.** Decoding dominates; little gain —
  rejected.

## 4. Design (approach A)

- **Reader count** `kBuiltPassReaders = 4`, capped at
  `max(1, hardware_concurrency / 2)`: enough to cut decode time several-fold, few
  enough to leave cores to the tile scheduler the user is waiting on. A
  constructor parameter (default from that rule) so tests can force 1 and N.
  **1 reader reproduces today's behaviour exactly.**
- **Window** `2 × readers` chunks claimed but not yet fed, plus the one the
  worker is feeding: window+1 = 9 chunks (9 × 4 MiB ≈ 36 MiB worst case).
- **Chunk order** is the existing band-major, left-to-right order; chunk *k*
  maps to (band, x) by the same arithmetic `run()` uses today.
- **Feeding** (`run()` thread): waits for slot *k*, then under the exclusive
  state lock `addChunk` or `failChunk` exactly as today; after the last chunk of
  a band, `notify(false)`; after the last band, the existing completion path
  (complete, log, `notify(true)`, then the cache save).
- **Readers** do only what the loop body does today outside the lock: the slide
  rectangle arithmetic, `m_inner->readBlock(..., mode)`, the size/type check,
  catching exceptions into "failed". They touch no accumulator or listener.
- **Cancel / unreliable / generation**: readers stop claiming when `m_cancel`
  or `sourceUnreliable()` is set; the feeder checks both before every chunk as
  today and abandons or returns exactly as today. `run()` joins its readers
  before it returns on every path (including exceptions), so the destructor's
  and `setColorMode`'s existing join of the worker still covers everything.
  A chunk read while the level was marked unreliable is never fed (same rule as
  today: checked after the read, before accumulating). With more than one
  reader, which chunks were already fed when an unreliable mark abandons the
  build is timing-dependent (as a mark from the tile scheduler already was); a
  distrusted chunk is still never fed.
- **One warning per build** for failed reads, as today (an atomic flag).
- **Logging**: the existing start line gains the reader count; the completion
  line is unchanged.
- The disk-cache load path runs before any reader starts, unchanged.

## 5. Tests (infra, `BuiltPyramidSourceTest`)

- Byte-identical: the same `FakeSource` built with 1 reader and with 4 readers
  gives identical bytes for every resident level (`levelBytes` via tiles or the
  overview), and identical `regionState` maps when one chunk fails.
- Concurrency happens: a fake whose `readBlock` records the maximum number of
  simultaneous calls sees > 1 with 4 readers (and exactly 1 with 1 reader).
- Window holds: a fake that blocks chunk 0 forever-until-released sees no more
  than `window` reads started before it is released.
- Cancel: destroying the source while readers are blocked in `readBlock`
  (released by the test on cancel) returns within 2 s and leaves nothing running.
- Unreliable mid-build with 4 readers: abandoned exactly as with 1 (no overview,
  unbuilt tiles read as errors).
- Three pre-existing tests were pinned to readers=1, because their assertions
  encode one-read-at-a-time ordering; the rest pass unchanged at the default
  reader count. A new 4-reader mode-change test covers a colour-mode toggle
  during a parallel build.

## 6. Manual check (group AP)

- AP1 `PIT_01_00248_01.tiff`, cache off: record the build time from the log
  against the 60.3 s baseline; the view fills as before; panning at full
  resolution during the build stays responsive.
- AP2 Close the slide mid-build: closes at once (log shows cancel).
- AP3 Toggle colour management mid-build: rebuilds in the new mode.

## 7. Left to the builder

Names, the exact synchronisation primitives (one mutex + condition variable
over the slot ring is enough), whether readers are `std::thread`s owned by
`run()` or a tiny local pool.

## 8. Results

| Check | Result |
|-------|--------|
| AP1 build time and responsiveness on `PIT_01_00248_01.tiff` | pass (controller, 2026-11-27): PIT_01_00248_01.tiff, cache off, 4 readers on a 24-thread machine: 22 598 ms vs 60 290 ms single-reader baseline (2.7x); responsiveness during the build not judged (no GUI input on the shared desktop) |
| AP2 close mid-build | not run (needs GUI input) |
| AP3 colour-management toggle mid-build | not run (needs GUI input) |

Note: on a source whose reads serialise (e.g. a striped GDAL TIFF, per-file mutex in
SlideIO), 4 readers queue up to 4 chunk reads ahead of the user's tiles, and a
close/toggle join can wait up to 4 chunk reads.
