# Plan: replace `rt.heap` (`unordered_map`) with a flat array

## Motivation

game4 is thread-bound (paint ≤0.2 ms; heavy frames spend ~325–350 ms entirely
in the task/thread loop). Profiling-off counters on a heavy 100k-step frame:

| frame | task | steps | heapProbes | probes/step |
|---|---|---|---|---|
| light | 22 ms | 6,379 | 1,707 | 27% |
| heavy | 352 ms | 100,096 (step-limit cap) | 34,672 | **35%** |

`heapProbes` counts per-opcode probes into `rt.heap` (getfield fused + standalone,
putfield, both invokeinterface receiver lookups). **One in three opcodes does a
`rt.heap` lookup** — a hash + bucket load + node-pointer chase + node deref, all
in PSRAM (3–4 dependent, cache-miss-prone loads). Replacing the hash map with a
flat array indexed by id collapses that to one contiguous indexed load + a deref.

### Why this is not the rejected inline cache

`docs/rejected/heap-lookup-options.md` reverted a **single-entry IC in front of**
`rt.heap`: every miss paid the IC check *plus* the original `heap.find` — strictly
more work, neutral avg / regressed tail. This plan **replaces** the map; there is
no cache layer, no miss path, no fallback — every access is one indexed load,
always. The documented failure mode cannot occur. That same doc explicitly parks
"direct array indexed by handle id, with free-list … not worth doing until
measurement shows `heap.find` is on the critical path" — game4's 34.7k probes/frame
is that measurement (game2 was <1% of budget).

Precedent: commit `8251c8e` ("heap object: replace stringkey map with indexed
vector") already applied this technique one level down (field name→value map inside
`HeapObject`) and kept it.

## Storage choice: `std::vector<std::unique_ptr<HeapObject>>` indexed by id

- Slot index = raw object id (1..N, dense — `nextObjectId` is sequential from 1;
  `objectId()` masks the handle down to this id). `nullptr` = empty slot.
- **Why `unique_ptr`, not by-value `vector<HeapObject>`:** the interpreter holds
  `HeapObject&` across calls that can allocate more objects. `unordered_map` gives
  reference stability today; `unique_ptr` preserves it (growing the outer vector
  only moves 8-byte pointers; each `HeapObject` keeps its address). A by-value
  vector would dangle on growth — rejected for correctness.
- **Access cost:** `vec[id]` (one contiguous, cache-friendly load) → deref.
  Replaces hash + bucket load + node-pointer chase + node deref. Fewer dependent
  PSRAM loads = the win.
- **Bounded:** the existing `freeObjectIds` free-list (`allocateHeapObjectId`
  pops it before bumping `nextObjectId`; GC pushes freed ids back) means the
  vector sizes to the *high-water-mark of simultaneously-live objects*, not total
  allocations.

## Migration: 2 steps behind an accessor indirection

Changing the `Heap` typedef breaks all ~20 sites at once — can't stage by site.
Stage by **indirection** instead:

### Step 1 — introduce accessors, keep `unordered_map` underneath (pure no-op refactor)

Add helpers that today just wrap the map:

```cpp
inline HeapObject* heapGet(Runtime& rt, uint32_t id);          // find→ptr, nullptr on miss
inline HeapObject& heapEmplace(Runtime& rt, uint32_t id, ...); // allocation
inline void        heapErase(Runtime& rt, uint32_t id);        // erase
template<class F> void forEachHeapObject(Runtime& rt, F&& fn);  // (id, HeapObject&) for GC sweep
```

Convert all call sites from `find/end/->second` / `operator[]` / `erase` /
range-for to these helpers. Behavior identical → build + full test suite green.
Isolates the ~20 mechanical edits from the storage change.

### Step 2 — swap the implementation behind the accessors only

Change `using Heap = std::vector<std::unique_ptr<HeapObject>>;`, reimplement the
4 helpers + `allocateHeapObjectId` (resize-to-fit, set slot) + the GC sweep
iteration. **Zero call-site changes.** Build + tests green. The actual storage
change is localized to ~5 functions.

### Step 3 — game4 A/B

Profiling-off, min/avg/max frame + `heapProbes` count sanity-check (should be
unchanged — same number of logical lookups, just cheaper). Then remove the
temporary `[frame-split]` / `heapProbes` diagnostics (or keep behind a flag).

## Exact sites to convert (Step 1)

Line numbers as of this plan; re-grep `rt.heap` before editing.

- **find→ptr (12):** 696, 842, 932, 1195, 1339, 1392, 1581, 1843 (fused
  getfield), 2576 (getfield 0xb4), 2863 / 2923 (invokeinterface 0xb9), 3166,
  3362 / 3651 (displayable lookup)
- **allocation `operator[]`:** 636, 642 → `heapEmplace`
- **putfield `rt.heap[*id]` (2625):** convert to `heapGet` + null-guard.
  *Verify the object always exists here* — current `operator[]` would silently
  insert a blank object at an arbitrary id (latent bug); confirm and guard rather
  than replicate.
- **GC sweep range-for (1286) + erase loop (1310–1325):** `forEachHeapObject`
  + `heapErase`.

## Risks & mitigations

1. **Replace, don't front** — no cache layer; this is the swap that sidesteps the
   rejected IC's miss-cost failure. ✓ by design.
2. **Reference stability** — `unique_ptr` preserves the `unordered_map`
   guarantee the code relies on; add a comment documenting the invariant.
3. **putfield insert-vs-get semantics** — verify the object always exists before
   changing (see site note above).
4. **Memory** — log max `heap.size()` on ESP32 to confirm the high-water-mark is
   bounded (expect low thousands); outer vector is ~8 B × that in PSRAM, SRAM
   untouched.
5. **id 0 unused** — slot 0 stays `nullptr`; `isNull()` already treats 0 as null.

## Validation

- Correctness: `make -C tools/jvm-poc test` green after Step 1 and Step 2.
- Performance: game4 total-frame A/B, profiling-off, read min/avg/max (not just
  avg — the rejected IC looked neutral on avg and regressed the tail).
- If a step regresses: record in `docs/rejected/heap-lookup-options.md` with the
  structural reason (per CLAUDE.md Optimisation Discipline).

## Result — Kept (win: ~5%, tail improved)

game4, profiling-off, steady-state windows (warmup window dropped):

| build | frame avg | min frame | fps |
|---|---|---|---|
| superinstruction (baseline) | ~388 ms | ~352–370 ms | 2.6 |
| + indirection only (Step 1) | ~397 ms | ~361–377 ms | 2.5 |
| **+ flat array (Step 2)** | **~368 ms** | **~333–348 ms** | **2.7–2.8** |

- **Step 2 vs baseline: −20 ms/frame (−5.2%)**, min-frame −18 ms — *both* avg and
  tail improved (the rejected IC's failure mode avoided, as predicted).
- Step 1 (accessor indirection, still `unordered_map`) cost +9 ms — the
  un-inlined helper calls. The storage swap (−29 ms vs Step 1) more than repays it.
- Adding `__attribute__((always_inline))` to the helpers on top of Step 2 changed
  nothing (same-to-slightly-worse) → gcc was already inlining them; the win is the
  storage shape, not call elision. `always_inline` **not** kept.

**Key insight that reframes the follow-ups:** a hash probe on *35% of all opcodes*
was worth only ~5% of frame time → ~120 cyc/probe, i.e. the object map mostly fit
cache; it was never the catastrophic PSRAM-miss cost feared. The remaining ~368 ms
(~3 µs/op on heavy frames) is **per-op execution + dispatch**, not handle lookups.
So further map-flattening has bounded upside; the real ceiling is dispatch cost.

## Phase 2 — array heaps (`rt.arrays` / `rt.primitiveArrays`) — DEFERRED

**Status:** Deferred (2026-06-15). The storage design is sound and the technique
is proven, but a scope discovery + a recalibrated upside made the trade
unfavourable. Recorded here so it can be picked up if the calculus changes.

### Why deferred
1. **Scope is ~3–4× the heap and crosses the native-method ABI.** Unlike
   `rt.heap` (interpreter-local), the array maps are exposed *by reference* in
   `NativeCallContext` (`native_methods.hpp:44-45`), so flattening the storage
   touches not just the ~16 interpreter sites + GC but **~26 native-method sites
   across 4 files** (`string.cpp`, `rms.cpp`, `image.cpp`, `system.cpp`) and the
   context type — ~42 sites total, changing the native ABI.
2. **Recalibrated upside ~1–2%, not 2–4%.** The Phase 1 Result showed each map
   probe is only ~120 cyc and mostly cache-resident (not a PSRAM-miss). Array
   probes are ~12–14% of ops (vs the object map's 35% → 5%), so scaling down
   gives ~1–2%. And the hot array access is entirely in the interpreter
   (`loadArrayElement`); the 26 native sites are cold churn, not perf.

~1–2% for a 42-site cross-ABI refactor, while already deep in diminishing
returns (per-op cost is dominated by neither dispatch nor lookups), is a poor
trade. Banking Phase 1 (kept, ~5%) and stopping was the call.

### If revisited
The design below still holds. Same technique, same justification — tracked here
rather than a separate doc because it is the identical optimisation on a sibling
structure.

- **Targets:** `rt.arrays` (`unordered_map<uint32_t, vector<Value>>`) and
  `rt.primitiveArrays` (`unordered_map<uint32_t, vector<int32_t>>`), probed by
  `baload`/`aaload`/`*aload`/`*astore`/`arraylength` — ~10–15% of game4 ops.
- **Bounded the same way:** array ids are recycled via `arraysToFree` in GC.
- **Expected upside:** smaller than the object map (fewer probes, and per the key
  insight above the per-probe cost is ~120 cyc, mostly cache-resident) — estimate
  ~2–4%. Worth it as a low-risk continuation, not a step-change.
- **Same migration shape:** accessor indirection (Step 1) → storage swap to
  `vector<unique_ptr<...>>` behind the accessors (Step 2). Two maps, so two
  parallel accessor sets (or a small templated helper).
- **Caveat:** primitive vs reference arrays are distinguished by which map holds
  the id (`primitiveArrays.erase(id) == 0 ? arrays.erase(id)`); preserve that
  disambiguation — a missing id must fall through to the other map, not assume.

### Decision: extend, don't fork

Kept in this doc (not a new `array-heap-flat-array.plan.md`) because the
motivation, recycling model, reference-stability rationale, "replace don't front"
principle, and measurement methodology are identical. A separate doc would
duplicate all of that. If Phase 2 lands, append its own Result block below.
