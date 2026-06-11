# Invoke* bytecode profiling findings

Findings from chasing the cost of `invokevirtual`/`invokespecial`/
`invokestatic`/`invokeinterface` (0xb6-0xb9) in slow frames using
per-opcode CCOUNT (`opcodeCycles[]`), `invokeNativeCycles`, and
`taskNativeProfiles_` (`native_wall` / `[slow-native]`).

Entries follow the template at the bottom of this file. Add new ones above
the template.

---

## native_wall vs invokeNativeCycles: ~8x gap is the render-loop paint() call

**Date:** 2026-06-11
**Status:** Diagnostic finding — root cause identified, no code change yet.

### Hypothesis
`[slow-cyc2]` showed `invokeNativeCycles` (CCOUNT around every native body
reached via 0xb6/0xb7/0xb8/0xb9) at ~10,000-11,800µs per slow frame, while
`native_wall` (sum of `taskNativeProfiles_[*].totalUs`, wall-clock per
native call) was only ~1,300-1,400µs — a ~7-9x gap. Per-method `cyc`
(CCOUNT) vs `total` (wall-clock) in `[slow-native]` matched within ~10-15%
(cyc slightly lower, as expected — CCOUNT excludes a little entry/exit
overhead that `nowUs()` includes), so the per-call measurement itself is
correct. The gap had to be native calls that add to `invokeNativeCycles`
but never reach `recordTaskNativeProfile`.

### Investigation
- `recordTaskNativeProfile` only runs when `tTaskNative != 0`, which
  requires `rt.host->profileTaskMethods && rt.currentTask != nullptr`.
- `rt.currentTask` is set to `&task` for the duration of each task's turn
  (interpreter.cpp ~3454) and reset to `nullptr` once the task loop
  finishes (~3538).
- The render loop's direct `paint()` dispatch (interpreter.cpp ~3652-3653)
  runs *after* that reset, via `pushJavaFrame` + `runTrampoline`, with
  `rt.currentTask == nullptr`.
- The normal MIDP repaint flow: a task does a little direct drawing (e.g.
  8 `drawImage` calls — these *are* tracked, `rt.currentTask != nullptr`),
  calls `repaint()` then `serviceRepaints()` (yields via `sleepThread(0)`,
  `wakeAtMillis = now`), and the render loop then calls `Canvas.paint()`
  for the actual frame composition — untracked.

### Finding
The ~8,700µs/slow-frame untracked portion of `invokeNativeCycles` is native
graphics calls (`fillRect`, `drawString`, further `drawImage`/`setClip`,
etc.) made from inside the render-loop's `paint()` invocation, which
structurally runs outside any task context. `taskNativeProfiles_` /
`native_wall` only ever sees the small slice of native calls a task makes
*before* yielding via `serviceRepaints()`. This is not a bug in the
profiling math — it's a blind spot from the `rt.currentTask != nullptr`
gating condition.

### Lesson
1. **`native_wall` / `[slow-native]` undercounts total native cost by ~8x
   on this workload.** Don't treat it as "the" native cost; use
   `invokeNativeCycles` (`[slow-cyc2] native=`) for the aggregate, and
   treat `[slow-native]` as "what the task itself drew before yielding"
   only.
2. Per-call CCOUNT vs wall-clock agreement (within ~10-15%) is now a
   validated baseline for any per-call instrumentation added around native
   dispatch — future mechanisms can be cross-checked the same way.
3. To break down the render-loop `paint()` native cost by method, the
   `tTaskNative` gate needs to fire during that call too — e.g. a sentinel
   "render" context, not strictly `rt.currentTask != nullptr`. Not done —
   see open follow-ups.

### Current opcode-cost snapshot (slow frame, ~125-175ms)
For prioritising the next step. Two samples shown — "baseline" predates the
`invokeNativeCycles`/`totalCycles` instrumentation added in this
investigation, "recent" is after:

| metric | baseline | recent |
|---|---|---|
| `invokeNativeCycles` (`native=`) | — | 10,022-11,834µs |
| `native_wall` | — | 1,318-1,416µs |
| `pushFrameCycles` (`push=`) | ~2,400µs | 2,414-4,589µs |
| `0xb6` invokevirtual | 23,701µs (72,928 c/op) | 15,886µs (136,169 c/op) |
| `0xb7` invokespecial | 6,193µs (2,576 c/op) | 13,535µs (5,800-6,050 c/op) |
| `0xb4` getfield | 7,347µs (591 c/op) | 11,981µs (969-996 c/op) |
| `0x32` aaload | 4,805µs (528 c/op) | 7,834µs (861-883 c/op) |
| avg fps (60-window) | 14.9 | 12.7-13.1 |
| frame max | ~125ms | ~175ms |

`0xb4`/`0x32` c/op is well above plain interpreter-loop overhead (`iload`
~155 c/op), pointing at PSRAM cache-miss cost on `fields[slot]` /
array-element access under load — `heap.find` itself was ruled out as
cheap by [heap-lookup-options.md](heap-lookup-options.md).

### Open follow-ups
- Give the render-loop `paint()` call a non-null `rt.currentTask` sentinel
  (or relax the `tTaskNative` gate) so `[slow-native]` shows the *actual*
  per-method breakdown of the ~8,700µs untracked native cost.
- Investigate `0xb4`/`0x32` PSRAM access cost directly — likely
  `fields[slot]` / `CompactArrayHeap`/`ArrayHeap` element access itself,
  not the `heap.find` lookup that precedes it.
- **Unexplained regression** between baseline and recent samples: c/op for
  `0xb6`, `0xb7`, `0xb4`, `0x32` and `pushFrameCycles` all increased, and
  fps dropped 14.9→12.7-13.1. Not yet root-caused — could be a different
  game-state phase, or a side effect of the larger
  `NamedProfileAccumulator`/`MethodProfile` structs (added `totalCycles:
  uint64_t` to each). Needs a same-phase A/B before drawing conclusions.

---

## Template — copy this for new entries

```
## <Short, specific name of the finding/attempt>

**Date:** YYYY-MM-DD
**Status:** Diagnostic (no change) | Reverted | Kept (neutral) | Shipped

### Hypothesis
One paragraph. What did we believe was slow / wrong, and what mechanism
would make it faster / right — or what discrepancy were we trying to
explain?

### Change / Investigation
3-6 bullets. What instrumentation was added or what code paths were traced.

### Measurement / Finding
Numbers and/or the root cause, in plain terms.

### Lesson
The generalizable principle. One short paragraph. Future-you reads only
this section when deciding whether to try a similar idea.

### Open follow-ups / Related ideas not tried
Concrete next steps, or variants that share the same root cause and
shouldn't be re-proposed without new evidence.
```
