# Parallel Built-Level Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read the built-level pass's source chunks with several threads while feeding the accumulator in the original order, so the build is faster and its output byte-identical.

**Architecture:** `infra::BuiltPyramidSource::run` keeps its one accumulator and its feeding loop; a few reader threads owned by `run()` claim chunk indices in band-major order, read them unlocked, and park results in a bounded reorder window that `run()` drains in order.

**Tech Stack:** C++17 (`std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`), Catch2 v3, spdlog.

**Spec:** `docs/superpowers/specs/2026-11-27-parallel-built-level-pass-design.md`

## Global Constraints

- Output is **byte for byte identical** to the sequential build: same chunks, same order into `addChunk`/`failChunk`, same failed regions.
- Reader count: default `min(4, max(1, std::thread::hardware_concurrency() / 2))`; **1 reader reproduces today's behaviour exactly**.
- Window: at most `2 × readers` chunks claimed but not yet fed.
- Readers never touch the accumulator, `m_stateMutex` or listeners; listeners are never called under `m_stateMutex` (existing rule).
- Cancel, unreliable source level, generation change, failed reads and exceptions behave exactly as today; `run()` joins its readers before returning on **every** path, so the existing join of the worker covers them. A chunk read while the source level was marked unreliable is never fed.
- One warning per build for failed reads (as today). The start log line gains the reader count.
- Disk-cache load runs before any reader starts; the save path after completion is unchanged.
- Never run app/infra/ui test executables directly; use `SLIDEIO_VIEWER_TEST_IMAGES=D:/Projects/slideio/images/images ctest --test-dir build/build -C Release --output-on-failure`. Build: `cmake --build build/build --config Release`. Commit on branch `feature/parallel-built-level-pass`; do not push.

## Review Focus

1. **A reader throws something other than std::exception, or `run()` throws between spawning and joining readers** — must never leave a joinable `std::thread` (that is `std::terminate`). Covered structurally: the reader pool is a std::optional on run()'s stack, destroyed during unwinding before the catch; the reader-side non-std throw is tested.
2. **Close while every reader is blocked inside `readBlock`** — the destructor must return once the reads return, with no reader left waiting on the window forever. Pinned in Task 1.
3. **The last band narrower than the window / a slide with fewer chunks than readers** — readers must exit cleanly. Pinned in Task 1 (tiny slide test).
4. **Unreliable marked while several chunks are in flight** — none of them fed. Pinned in Task 1.
5. **Colour toggle mid-build with 4 readers** — the old build's readers all joined before the new build allocates. Covered by existing toggle tests run at the default reader count.

---

### Task 1: Parallel readers in the built-level pass

**Files:**
- Modify: `src/infra/include/slideio/viewer/infra/BuiltPyramidSource.h`, `src/infra/src/BuiltPyramidSource.cpp`
- Test: `tests/infra/BuiltPyramidSourceTest.cpp`

**Interfaces — Produces:**

```cpp
// BuiltPyramidSource
static int defaultBuiltPassReaders();   // min(4, max(1, hardware_concurrency / 2))
explicit BuiltPyramidSource(std::shared_ptr<core::ISlideSource> inner,
                            size_t budgetBytes = core::kBuiltLevelBudgetBytes,
                            int bandChunkMaxBytes = kDefaultBandChunkMaxBytes,
                            int readers = defaultBuiltPassReaders());   // clamped to >= 1
```

- [ ] **Step 1: Failing tests** (tag `[BuiltPyramidSource][parallel]`), using `FakeSource` with a small `bandChunkMaxBytes` so a band has several chunks; extend `FakeSource` minimally (a deterministic per-pixel pattern from (x, y) if it lacks one, a max-concurrent-`readBlock` counter, a per-chunk hold):
  - `four readers build the same bytes as one`: a 4100×3000 fake whose pixels depend on position; build with readers 1 and 4; every resident level's bytes are equal (read all resident tiles, or compare `smallestBuiltLevel()` plus every tile of every resident level).
  - `a failed chunk fails the same region with four readers`: `throwAt(y)` for one band; `isTileReady`/error-tile answers for every resident tile equal between readers 1 and 4.
  - `four readers overlap and one does not`: max simultaneous `readBlock` calls > 1 with 4 readers, == 1 with 1 reader (the fake sleeps ~5 ms per read to make overlap observable).
  - `the window bounds reads ahead`: hold chunk 0 until released; with 4 readers, the number of reads started before release is ≤ 8.
  - `destroying during blocked reads returns promptly`: `blockReads()`, wait until blocked, destroy the source in a thread, `release()` when the fake sees cancel (or immediately after starting the destroy); destruction finishes within 2 s.
  - `unreliable while chunks are in flight is abandoned`: with 4 readers, mark the source level unreliable while reads are held; release; no overview, unbuilt resident tiles read as errors, completion notified once.
  - `a slide with fewer chunks than readers completes`: a 600×400 fake with readers 4 completes and matches readers 1.
- [ ] **Step 2: Run** infra-tests under ctest — the new tests fail to compile (no `readers` parameter).
- [ ] **Step 3: Implement.** Keep the band/chunk arithmetic in one helper used by both readers and feeder (chunk index → band y, h, x, w, slide rect). Readers claim `next` under the window mutex while `next < fed + window` and not cancelled/unreliable; the feeder waits on the slot for chunk `fed`, then follows today's loop body exactly (unreliable check, exclusive lock, generation check, add/fail, band-end `notify(false)`). Wrap reader spawning and the feed loop so readers are signalled to stop and joined on every exit path, including exceptions. Log the reader count on the start line.
- [ ] **Step 4: Run** all four suites under ctest — pass, including every pre-existing BuiltPyramidSource test unchanged.
- [ ] **Step 5: Commit** — `"Read the built-level pass with several threads"`.

---

### Task 2: Documentation

**Files:**
- Modify: `CLAUDE.md` (built-levels key-decision bullet: one or two sentences — reader count and window, in-order feeding keeps output byte-identical, 1 reader is the old behaviour)
- Modify: `docs/superpowers/specs/2026-11-27-parallel-built-level-pass-design.md` (add `## 8. Results` with AP1–AP3, every row "not run")

- [ ] **Step 1:** Edit both files, keeping CRLF line endings.
- [ ] **Step 2: Commit** — `"Docs: parallel built-level pass"`.

The controller runs AP1–AP3 after the final review against a scratch-installed build and records them in §8.
