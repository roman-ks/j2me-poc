# Field-slot inline cache (getfield/putfield)

A direct-mapped, array-indexed inline cache placed in front of
`Runtime::fieldIndexCache` to resolve `(executing cls, cpIdx) → field slot`
without hashing. Reverted — net regression in clean measurement.

This is a sibling to [heap-lookup-options.md](../rejected/heap-lookup-options.md):
same root cause (a microcache over a PSRAM map that wasn't actually the
bottleneck), reached via a worse path (the bottleneck was a *profiling
artifact*). Kept separate because the failure mode here is specifically
about **trusting cyc-bracket measurements of tiny hot regions**, which is
worth its own entry.

Entries follow the template at the bottom. Add new ones above it.

---

## Direct-mapped per-site field-slot IC

**Date:** 2026-06-11
**Status:** Reverted — clean (profiling-off) A/B regressed avg, min, and max.

### Hypothesis
Per-opcode CCOUNT splitting of getfield (0xb4) reported the
`fieldIndexCache.find` step at **~240 cycles/op** — ~5× `heap.find`
(~50 c/op) and ~2× the `obj.fields[slot]` vector read (~115 c/op). The map
was confirmed spilled to PSRAM (`SramAllocator` silent fallback; internal
SRAM measured at **0 KB free**), and its key `(cls, cpIdx)` changes every
getfield, so it has zero temporal locality → a cold PSRAM bucket+node chase
on every op. A direct-mapped array IC (array index, no hashing, no
node-chase) keyed per call-site — which is stable for the session because
field layout is inheritance-stable — looked like the textbook fix CLAUDE.md
endorses ("array index, bytecode rewriting, or per-call-site immediate
state").

### Change
- Added `fieldSlotICKey[512]` (uint64) + `fieldSlotICSlot[512]` (uint16) to
  `Runtime` (~5 KB), keyed by `callCacheKey(&cls, cpIdx)`, indexed by a
  fibonacci-hash of the key.
- getfield/putfield check the IC first (L1); on miss fall through to
  `fieldIndexCache` (L2) and then the cold `buildFieldSlots` resolve, filling
  the IC. No invalidation (mapping is session-stable).
- Measured hit rate after warmup: **1.000** (2995 hits, 0 misses/frame) —
  the cache *worked* exactly as designed.

### Measurement
Clean build, **all frame profiling disabled**, game2, steady state:

| metric | pre-IC (`fd07897`) | IC (`72d7dce`) | Δ |
|---|---|---|---|
| avg fps (60-window) | 16.0 | 15.7 | **−0.3** |
| min fps | 9.2 | 8.8 | **−0.4** |
| frame avg | ~62.7 ms | ~63.6 ms | +0.9 ms |
| frame max | ~108.5 ms | ~114.2 ms | **+5.7 ms** |

Consistent across every window — not noise. With profiling off the only
behavioral delta between the two commits is the IC itself, so the IC owns
the regression.

The *profiled* run had looked favorable (the bracketed `field_access` slice
dropped, min fps +0.2) — which is precisely why it was misleading.

### Root cause
Two compounding measurement errors, both already warned about in
heap-lookup-options.md:

1. **The 240 c/op that justified the IC was a profiling artifact.** It was
   measured with `cpuCycles()` brackets wrapping the find. Bracketing a
   ~50-cycle operation with two `rsr.ccount` reads, an accumulator store, and
   the compiler-optimization barriers those impose inflates the region 3–5×.
   The real clean cost of the hash find was a fraction of 240 — the find was
   never the dominant cost, same conclusion heap-lookup-options.md reached for
   `heap.find`.

2. **The "field_access dropped" confirmation was the same bracketed slice,
   blind to the IC's non-local costs.** The bracket sees only the lookup
   region. It cannot see that the IC adds, per getfield+putfield (~4000
   ops/frame):
   - a 64-bit multiply (fibonacci index), and
   - a **~5 KB scattered-access footprint** in PSRAM (`fieldSlotICKey` +
     `fieldSlotICSlot`), whose hash-scattered reads evict the surrounding hot
     data — the `heap.find` buckets, the `obj.fields` vector, the next
     opcodes' operands — none of which the `field_access` bracket covers.

So the IC traded one PSRAM-scattered structure (the hash) for another (the
array) **plus** a per-op multiply **plus** more cache footprint. An IC only
beats a PSRAM hash if the IC itself lives in fast memory; with internal SRAM
at 0 KB free it could only ever land in PSRAM, where it is strictly more work.

### Lesson
1. **Never justify an optimization with a cyc-bracketed measurement of a tiny
   hot region.** The bracket overhead is a large fixed fraction of a sub-100-
   cycle op, and it is blind to non-local (cache-pollution) costs. The only
   trustworthy verdict is a **clean, profiling-off, total-frame A/B** on
   min/avg/max. Here profiled said "win," clean said "−5.7 ms max."
2. **An inline cache over a PSRAM structure cannot win while it also lives in
   PSRAM.** It needs fast backing memory to convert "avoid the hash" into a
   real saving; otherwise it is hash-cost ± ε plus its own footprint. On a
   board with 0 KB free internal SRAM, no such IC pays off — verify SRAM
   headroom *before* designing one.
3. This is the second microcache (after the single-entry heap IC) to regress
   for the same family of reasons. Treat "add a cache in front of a PSRAM map
   that profiling says is hot" as a red flag, not a lead.

### Related ideas not tried
- **Pin the IC to internal SRAM** — impossible here (0 KB free). Would only
  be worth revisiting if the SRAM budget changes materially (e.g. framebuffer
  moved off internal SRAM), and even then must clear a clean A/B first.
- **pc-indexed per-method quicken table** (spatial locality: the executing
  method's bytecode region is hot) instead of a global hash-scattered array.
  Plausibly avoids the cache-pollution failure, but it is still PSRAM-backed
  and far more invasive (per-method storage or bytecode mutation). Not worth
  it until a clean measurement shows field-slot resolution is actually on the
  critical path — as of this entry, it is not.
- **Shrink `Value` / store small objects' fields inline** to cut the
  `obj.fields[slot]` PSRAM read (~115 c/op, the part of getfield that is
  real). Different target entirely; see field-layout work, not lookup caching.

---

## Template — copy this for new entries

```
## <Short, specific name of the attempt>

**Date:** YYYY-MM-DD
**Status:** Reverted | Kept (neutral) | Kept (win: …) | Partially kept (…)

### Hypothesis
What did we believe was slow, and what mechanism would make it faster?

### Change
3–6 bullets. What code actually changed, terse.

### Measurement
Numbers. Before / after. State the noise floor and the profiling state.

### Root cause
Why the result came out the way it did. Mechanism, not vibes.

### Lesson
The generalizable principle. One short paragraph. Future-you reads only this
section when deciding whether to try a similar idea.

### Related ideas not tried
Variants sharing the same root cause; list them so they don't get re-proposed.
```
