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

## Out of scope

`rt.arrays` / `rt.primitiveArrays` (the `baload`/`aaload` maps) — separate
structures, *not* in the `heapProbes` count. Same treatment is a candidate
follow-up **if** the post-change A/B shows array-map probes are now the bottleneck.

## Validation

- Correctness: `make -C tools/jvm-poc test` green after Step 1 and Step 2.
- Performance: game4 total-frame A/B, profiling-off, read min/avg/max (not just
  avg — the rejected IC looked neutral on avg and regressed the tail).
- If a step regresses: record in `docs/rejected/heap-lookup-options.md` with the
  structural reason (per CLAUDE.md Optimisation Discipline).
