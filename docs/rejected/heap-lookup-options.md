# Heap lookup options

Attempts to make `rt.heap.find(uint32_t)` cheaper on the dispatch hot path.
This file is the running record so a future investigator doesn't repeat the same dead ends.

Entries follow the template at the bottom of this file. Add new ones above the template.

---

## Single-entry inline cache on `Runtime`

**Date:** 2026-06-04
**Status:** Reverted — avg neutral, **tail regressed** (max frame +5ms, min fps 5.9 → 5.7).

### Hypothesis
`rt.heap.find(*id)` is called several times per dispatched bytecode (getfield, putfield, invokevirtual receiver-class lookup, invokevirtual native-receiver lookup) and an earlier instrumented run reported "Heap find time: 26294us" per frame — ~32% of an 80ms frame. A 1-entry IC keyed on `lastHeapId` should collapse most of that to ~3 instructions on hit, since consecutive bytecodes typically operate on the same receiver.

### Change
- Added `lastHeapId` / `lastHeapObj*` to `Runtime`.
- Added `heapFind` (find-style, nullptr on miss) and `heapAt` (`operator[]`-style, always caches) inline helpers with `__attribute__((always_inline))`.
- Wired into the 4 hot dispatch sites + the `new`-init allocation path.
- Invalidated at the GC erase site (`heap.erase` then clear cache if id matches).
- `std::unordered_map` keeps element pointers stable across insertions, so no other invalidation needed.

### Measurement

| metric | baseline | IC (`always_inline`, no counters) |
|---|---|---|
| avg fps (60-frame window) | 12.9–13.2 | 12.8–13.2 |
| min fps | **5.9** | **5.7–5.8** |
| max fps | 16.4 | 16.4 |
| frame avg | ~76.7ms | ~76.8ms |
| **frame max** | **~169.5ms** | **~175ms** |

Avg is within noise. Min fps and max frame consistently worse across all 7 IC samples — this is a real tail regression, not noise.

Hit rate: 95–100% per frame (varies). The 5% miss path on heavy frames is doing damage.

Prior variants (also tried, also worse):
- **Without `always_inline`**: -0.6 fps avg. Helper called out-of-line — confirmed via `nm`/`objdump` showing 5 call instructions to the local symbol. Dispatch function exceeded GCC's `-O2` inline budget.
- **With hit/miss counters threaded through `rt.trace.frameProfile`**: further regression from 3-level chained dereferences on every call.

### Root cause
The work we were trying to remove was already cheap, and the cache check has non-zero cost on miss:
- `std::hash<uint32_t>` is identity in libstdc++ — no actual hashing.
- IDs are dense and monotonic, load factor is low, so bucket walks are typically one node.
- Total per-call cost ≈ 25–40 cycles on xtensa; per-call savings from the IC ≈ 20–25 cycles on hit.
- ~1300 calls/frame × ~100ns saved per hit ≈ 130µs out of a 77ms budget — below the noise floor on the **avg**.

But the **tail** tells a different story. Heavy frames (paint with lots of distinct sprites, frames that trigger GC, frames touching many objects in alternation) drop the hit rate. Every miss now pays the *original* `heap.find` cost **plus** the IC compare overhead — strictly more work than baseline. The avg is dominated by light frames where the cache hits; the max is dominated by heavy frames where it doesn't. So you see "neutral avg, regressed max" exactly as observed.

The earlier 26ms-per-frame figure for `heap.find` was measured with bytecode profiling enabled, which adds per-call timer overhead that dwarfs the underlying op. The real cost was always small.

### Why it appeared worse before fixes
1. **Counters** (`++rt.trace.frameProfile.heapCacheHits`) added 3-level chained dereferences on every dispatch — net regression even at 100% hit.
2. **No `always_inline`**: the 3000-line dispatch function exceeded GCC's `-O2` inline budget, so helpers became real `call` instructions. Per-call function-call overhead exceeded the savings.

Both are necessary conditions for the IC to be neutral. Neither makes it a win.

### Lesson
1. For maps with cheap hashes and dense keys (`unordered_map<uint32_t, …>` at low load factor with identity hash), do not assume `find` is expensive. Sanity-check: estimate `calls/frame × ~150ns` and compare against the frame budget — if it's <1%, no microcache will move the needle.
2. **Always read min/max alongside avg.** A change that's "flat on avg" can still regress the tail, and the tail is what defines perceived smoothness on a game. The avg here said "neutral"; the max said "−5ms per heavy frame." We'd have shipped a regression if we'd stopped at avg.
3. Profiling overhead inflates per-op timings non-linearly. A "26ms in heap.find" number under `JVM_ENABLE_BYTECODE_PROFILING=1` is not the same metric as 26ms of real cost.
4. An inline cache only pays off if `cost(check) × (1 - hit_rate) < cost(saved_work) × hit_rate`. With cheap saved work and a hit rate that drops under load, the inequality flips on exactly the frames that matter.

### Related ideas not tried
- **Flat open-addressed map** (`absl::flat_hash_map` / `ankerl::unordered_dense`). Cuts the bucket-pointer indirection. Likely also <1% on this workload — same reasoning.
- **Direct array indexed by handle id**, with free-list. Zero hashing, one load. Would have to budget for high-water-mark of live IDs; for the current handle scheme this is small.
- Neither is worth doing until measurement shows `heap.find` is actually on the critical path. As of this entry it is not.

---

## Template — copy this for new entries

```
## <Short, specific name of the attempt>

**Date:** YYYY-MM-DD
**Status:** Reverted | Kept (neutral) | Kept (win: …) | Partially kept (…)

### Hypothesis
One paragraph. What did we believe was slow / wrong, and what mechanism would make it faster / right?

### Change
3–6 bullets. What code actually changed, terse.

### Measurement
Numbers. Before / after. State the noise floor when relevant.

### Root cause
Why the result came out the way it did. Mechanism, not vibes. Cite the structural reason the change can't pay off (or did pay off).

### Lesson
The generalizable principle. One short paragraph. Future-you reads only this section when deciding whether to try a similar idea.

### Related ideas not tried
Variants that share the same root cause and therefore should not be tried without new evidence. List explicitly so they don't get re-proposed.
```
