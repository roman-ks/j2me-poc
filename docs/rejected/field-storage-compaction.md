# Compact HeapObject field storage (`vector<Value>` → `vector<int32_t>`)

Storing object instance fields as untagged 32-bit words instead of 12-byte
tagged `Value`s, recovering type from the field descriptor (per-call-site
`kind` for getfield/putfield width, per-class `fieldRefSlots` for GC). A
long/double occupies two consecutive words. Reverted — the memory win was
negligible and in the wrong pool, and the perf was neutral-to-negative.

Sibling to [field-slot-inline-cache.md](field-slot-inline-cache.md) and
[heap-lookup-options.md](heap-lookup-options.md): all three chased the
getfield/field-access cost and found that the *measured* sub-costs did not
translate into a real, bankable win. This one is specifically about
**optimizing the wrong resource** — saving PSRAM when SRAM is the binding
constraint. The original forward plan is preserved at
`docs/field-storage-compaction.plan.md`.

Entries follow the template at the bottom. Add new ones above it.

---

## vector<int32_t> field words + descriptor-typed access

**Date:** 2026-06-14
**Status:** Reverted — negligible memory win (wrong pool), perf flat-to-down.

### Hypothesis
getfield's residual cost was the `obj.fields[slot]` read (~115 cycles/op,
measured), because `HeapObject::fields` is `std::vector<Value>` at 12 bytes/
element — int/ref/short/etc. waste 8 of 12 bytes, so the field vector spans
more cache lines than necessary. Packing fields to 4-byte words (8 for
long/double) should (a) cut field-storage memory ~3× and (b) speed up the
read via better cache-line density.

### Change
- `HeapObject::fields`: `vector<Value>` → `vector<int32_t>`; long/double take
  two consecutive word slots (JVM two-slot model; avoids 8-byte alignment).
- `FieldKind`/`FieldSlot{wordIndex,kind}` as the value of `fieldSlotCache`/
  `fieldIndexCache`; `buildFieldSlots` assigns word offsets + `kind`.
- `readFieldSlot`/`writeFieldSlot` reconstruct a `Value` (`ofInt`/`ofLong`)
  on every getfield/putfield/native field access.
- GC `markValue` rewritten to trace only `fieldRefSlots` (descriptor-derived
  reference slots) instead of scanning every field — required because untagged
  long words can alias a handle bit pattern. (Validated by the
  `gc long field alias` regression test, which has teeth.)
- Forced a prerequisite: Font/Display were `kStr` fake-handles that corrupt in
  a reference-typed word field, so they were migrated to real H1 handles
  (`0x05`/`0x06`). See note below.

### Measurement
ESP32-S3, game2, profiling off, ~11–14 steady 60-frame windows each; memory
read once (identical every window). Baseline = `fd07897`/`ca3ec61`.

| metric | baseline | compaction+handles | Δ |
|---|---|---|---|
| avg fps | ~16.0 | ~15.8 | −0.2 |
| min fps | 9.2 | 8.9 | −0.3 |
| frame avg | 62.6 ms | 63.4 ms | +0.8 ms |
| frame max | 108.5 ms | 112.5 ms | +4 ms |
| PSRAM used | — | — | **−1708 B** |

"Compaction only" (no handle migration) was indistinguishable: avg ~15.7,
min 9.1, frame avg ~63.6 ms. Noise floor matters here: a single unrelated
one-line change was earlier shown to move frame-avg ~1.6 ms via i-cache
alignment alone, so the ~0.8–1 ms perf delta is *within layout noise* — i.e.
not a proven regression, but definitively **not an improvement**.

### Root cause
Both halves of the justification failed:

1. **Memory: negligible and in the wrong pool.** −1708 B against **7.3 MB
   free PSRAM** is 0.02%. `HeapObject::fields` lives in PSRAM (default
   `operator new`), which is abundant; the *binding* constraint is internal
   **SRAM**, measured at **0 KB free** — and the compaction never touches it.
   The 3×-per-field density is real but multiplies a small base: J2ME games
   simply don't hold much field data, so the absolute saving is tiny, and it
   accrues to the resource that isn't scarce.

2. **Speed: the density gain didn't beat the reconstruction cost.** The field
   read was a real ~115 c/op, but the field working set is small enough to sit
   in the PSRAM data cache already, so denser packing bought little; meanwhile
   getfield/putfield now run a `kind` branch + `ofInt`/`ofLong` reconstruction
   on the two hottest opcodes. Net: flat-to-slightly-worse.

So a real measured sub-cost (the field read) did not imply a bankable win:
the saving landed in the wrong pool and the per-access overhead offset the
cache benefit.

### Lesson
Before compacting a data structure for memory, confirm **which pool it lives
in and whether that pool is the constraint**. Here fields are PSRAM (7+ MB
free) while SRAM is exhausted — shrinking PSRAM is effort spent on the
non-binding resource. And a per-element size win only matters at scale: 3× a
small absolute footprint is still small. Pair any "denser = fewer cache
misses" claim with a clean total-frame A/B — packing that adds per-access
reconstruction can erase its own cache benefit, and at these magnitudes the
result is buried under i-cache-alignment noise anyway.

### Related ideas not tried
- **Move fields to SRAM** to attack the binding pool — infeasible: SRAM is at
  0 KB free; adding field storage there would worsen the actual constraint.
- **Untagged words without reconstruction** (store the raw word, hand it to
  the interpreter as-is) — can't: the operand stack/locals are tagged `Value`s,
  so getfield must produce a tagged value; reconstruction is unavoidable.
- **Compact only for field-heavy apps** — violates the generic-runtime rule
  (no per-app layout), and the win would still be PSRAM, not SRAM.
- **Shrink `Value` itself (12→8)** — separate idea; rejected earlier because
  `int64` inline forces 12 B on Xtensa and boxing longs regresses long-heavy
  apps. Same wrong-pool caveat would apply.

### Note: the Font/Display kStr→H1 handle migration
The compaction forced converting Font (`font:default`) and Display
(`display#1`) from `kStr` fake-handles to real H1 handles. That conversion is
a defensible cleanup on its own, but in isolation it cost ~0.4 fps via pure
i-cache-alignment (confirmed: a non-handle constant `0x12345678` and a
`0xff`-prefixed value degraded identically, so it's codegen layout, not the
value), and it has **no functional benefit without compaction**. Reverted
alongside. Revisit only if some other feature genuinely needs Font/Display to
be real object handles.

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
