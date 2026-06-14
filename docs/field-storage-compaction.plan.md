# Plan: compact `HeapObject` field storage (`vector<Value>` → `vector<int32_t>`)

**Status:** Proposed. Not started.
**Goal:** cut object field storage from 12 bytes/slot to 4 bytes/slot (8 for
long/double) by storing fields as untagged 32-bit words, using the field
descriptors — which are statically known — to recover type on access and to
drive GC reference tracing.

This is the "typed field storage" direction from
[docs/rejected/field-slot-inline-cache.md](rejected/field-slot-inline-cache.md):
after two failed attempts to make the *slot lookup* cheaper, the measured
residual getfield cost is the **field data read** (`obj.fields[slot]`, ~115
cycles/op on ESP32), which is the field vector being wide and cache-sparse.
Shrinking the element is the lever; the lookup is not.

---

## 1. Why

- **Footprint (definite win):** `HeapObject::fields` is `std::vector<Value>`
  at 12 bytes/element. Most fields are `int`/reference/`short`/`char`/`byte`/
  `boolean`/`float` — all 32-bit — so 8 of every 12 bytes are wasted. At
  4 bytes/word that is a **3× reduction** in per-object field memory, paid
  straight back to PSRAM/SRAM budget on a memory-constrained target.
- **Speed (likely, must measure):** denser fields = fewer cache lines per
  object = fewer PSRAM misses on the `obj.fields[slot]` read that dominates
  getfield today. Estimated ~0.5–1.5 ms/slow-frame, but this **must** be
  proven with a clean profiling-off min/avg/max A/B — see §8. We have twice
  been fooled by bracketed sub-region measurements; total-frame is the only
  verdict.

### 1.1 Does the cache-value change eat the savings? No.

The width metadata rides in the slot cache, so it's worth confirming the cache
cost is negligible against the field savings:

- **Cache cost ≈ 0.** We store the `FieldSlot` struct (4 B) as the cache value
  rather than a bare `uint16_t` (2 B), but it's free: the cache node is
  `pair<uint64_t, FieldSlot>`, and the 8-byte key forces 16-byte node padding
  either way (`8+2→16` and `8+4→16`), so the larger value grows into existing
  padding. The node is ~28 B regardless.
- **The two scale differently.** Cache size is **O(code)** — one entry per
  `(cls, cpIdx)` field-access *site*, measured at ~130 and **flat regardless of
  object count**. Field storage is **O(live objects)** — it grows with every
  object allocated. Saving 8 B/slot (12→4; longs 12→8) across thousands of live
  field slots is KB-to-tens-of-KB, climbing with the workload, against a fixed
  ~0–260 B cache delta. Not a wash — ~1:50+.

## 2. Current state (verified)

- `HeapObject::fields` — `std::vector<Value>`, indexed by slot
  ([interpreter.cpp:273](../tools/jvm-poc/interpreter.cpp#L273)).
- **One slot per field, regardless of width**
  ([buildFieldSlots:535](../tools/jvm-poc/interpreter.cpp#L535) `nextSlot++`).
  A `long` field today is a single 12-byte `Value` (kLong arm). There is **no**
  2-slot hole today; the waste is purely element width.
- Fields hold `kInt` (ints, floats-as-bits, and tagged handles), `kLong`
  (longs, doubles-as-bits), or `kNone` (uninitialized → getfield returns 0).
  Fields never hold `kStr` (debug strings) in normal operation — **audit this
  assumption**, see §7.
- GC traces object references by scanning **every** field `Value` and asking
  `objectId`/`arrayId` ([markValue:1199](../tools/jvm-poc/interpreter.cpp#L1199)).
  Safety rests on `objectId` requiring `tag == kInt`
  ([objectId:905](../tools/jvm-poc/interpreter.cpp#L905)) — a `kLong` value can
  never be mistaken for a handle. **This tag check is load-bearing for GC.**

## 3. Target design

`HeapObject::fields` becomes `std::vector<int32_t>` of **words**:

- `int / short / byte / char / boolean / float / reference / array-ref` →
  **1 word**.
- `long / double` → **2 consecutive words** (the JVM's own two-slot model;
  avoids any 8-byte alignment concern on Xtensa — all access is 32-bit).
- The word array is **zero-initialized**, which is exactly the JVM default
  (`0 / null / 0L / 0.0`). The `kNone`/`isInitialized()` concept disappears for
  fields — a never-written field reads back 0, same as today's behavior.

A field's type is recovered from its descriptor, cached at class-load. The hot
path needs the slot's **kind** (currently just word vs long → 1 or 2 words,
reconstruct as `kInt`/`kLong`); GC needs the set of reference slots. Both are
computed once in `buildFieldSlots`.

### Slot-cache value type

The `(cls,cpIdx)→slot` and `name→slot` caches change their value from a bare
`uint16_t` to a small struct, so the kind travels with the index in one lookup:

```cpp
enum class FieldKind : uint8_t {
    kWord = 0,  // 1 word; reconstruct ofInt  (int/short/byte/char/bool/float/ref)
    kLong = 1,  // 2 words; reconstruct ofLong (long/double)
    // room to grow: kRef, narrowed int widths, etc. — not needed yet
};
struct FieldSlot {
    uint16_t  wordIndex;        // position in HeapObject::fields (NOT the value)
    FieldKind kind = FieldKind::kWord;
};  // 4 bytes (2 + 1 + 1 pad); the kind is the only thing the hot path decodes
```

We deliberately use the struct over bit-packing `kind` into the `uint16_t`'s
top bit: it costs nothing (the cache node is `pair<uint64_t, FieldSlot>`, and
the 8-byte key pads the node to 16 either way — §1.1), it's clearer, and it
leaves a typed extension point for future per-kind handling. `kind` only needs
`kWord`/`kLong` for now.

### 3.1 Reconstruction table (descriptor first char → access)

| descriptor[0] | words | getfield reconstructs | putfield stores |
|---|---|---|---|
| `I Z B C S` | 1 | `Value::ofInt(words[s])` | `words[s] = v.i32` |
| `F` (float bits) | 1 | `Value::ofInt(words[s])` | `words[s] = v.i32` |
| `L… […` (ref) | 1 | `Value::ofInt(words[s])` (handle) | `words[s] = v.i32` |
| `J` (long) | 2 | `ofLong((u32)words[s] \| ((i64)words[s+1] << 32))` | split v.i64 lo/hi |
| `D` (double bits) | 2 | same as `J` | same as `J` |

Endianness convention (pick once, use everywhere): **`words[s]` = low 32 bits,
`words[s+1]` = high 32 bits.**

## 4. Slot model & per-class metadata (`buildFieldSlots`)

[buildFieldSlots:514](../tools/jvm-poc/interpreter.cpp#L514) changes:

1. **Slot width.** Advance `nextSlot` by `width(descriptor)` (2 for `J`/`D`,
   else 1) instead of `++`. `nextSlot` now counts **words**; the returned count
   pre-sizes `obj.fields` ([allocateObject:644](../tools/jvm-poc/interpreter.cpp#L644)).
2. **Store `FieldSlot` (wordIndex + kind) as the cache value.** The
   `fieldSlotCache` inner map and the `fieldIndexCache` (cpIdx→slot) change
   their value type from `uint16_t` to `FieldSlot` (§3 "Slot-cache value
   type"). `buildFieldSlots` sets `kind = kLong` for `J`/`D` fields, `kWord`
   otherwise. The getfield hot path reads `FieldSlot` in its existing single
   cache lookup and branches on `kind` — no second table touched for width.

   **What `kind` is and is not.** It is purely a *hot-path width hint* for
   getfield/putfield: the access operand (the `Fieldref` descriptor) already
   determines the field's width, but the slot cache deliberately does not carry
   the descriptor, so on a cache hit the handler would otherwise have to
   re-resolve it just to know "1 word or 2." `kind` caches that alongside the
   index it's already fetching. It is **not** object storage and **not** how GC
   avoids mis-tracing longs — that is `fieldRefSlots` (§6); a long's two words
   are simply never in the ref-slot list, so GC never looks at them. The two
   metadata serve different consumers (getfield width vs GC ref-set) and are
   not redundant. (Note `kind == kWord` does *not* distinguish ref from int —
   it doesn't need to; getfield reconstructs both as `ofInt`, and ref-ness is
   GC's concern via `fieldRefSlots`.)
3. **Reference-slot list (for GC).** New per-class structure on `Runtime`:
   ```cpp
   std::unordered_map<const ClassFile*, std::vector<uint16_t>> fieldRefSlots;
   ```
   For each field whose descriptor[0] is `L` or `[`, append its `wordIndex`.
   Like `fieldSlotCache`, this **inherits the parent class's ref-slots** first
   (mirror the existing superclass merge at
   [buildFieldSlots:522-526](../tools/jvm-poc/interpreter.cpp#L522-L526)).

## 5. Access-path changes

All sites that touch `obj.fields` as `Value`:

| site | change |
|---|---|
| getfield `0xb4` ([~2510](../tools/jvm-poc/interpreter.cpp#L2510)) | read `FieldSlot`; branch on `kind`; read 1 or 2 words; reconstruct per §3.1 |
| putfield `0xb5` ([~2558](../tools/jvm-poc/interpreter.cpp#L2558)) | read `FieldSlot`; `resize(wordIndex + width)`; write 1 or 2 words |
| `writeField`/`setField` ([683](../tools/jvm-poc/interpreter.cpp#L683)) | name→`FieldSlot` carries `kind`; write 1/2 words from the `Value` |
| `readFieldValue` ([1336](../tools/jvm-poc/interpreter.cpp#L1336)) | name→slot; reconstruct 1/2 words to `Value`; drop `isInitialized()` (zero = default) |
| `allocateObject` ([644](../tools/jvm-poc/interpreter.cpp#L644)) | `resize(wordCount)` — already uses the builder's return, now word count |
| commented debug dumps ([3352](../tools/jvm-poc/interpreter.cpp#L3352), [3549](../tools/jvm-poc/interpreter.cpp#L3549)) | leave commented / update if reactivated |

`Value` itself is **unchanged** (still 12 bytes) — only field *storage* changes.

## 6. GC change (the load-bearing part)

The tag is what currently stops a `long`'s bit pattern from being mis-traced as
a reference. Untagged words remove that protection: a long's two halves are
arbitrary 32-bit values, and any half in `0x01000000–0x04FFFFFF` would alias a
handle. So GC **cannot** keep scanning all fields heuristically — it must trace
by descriptor-derived ref-slots.

Rewrite the object branch of
[markValue:1190-1203](../tools/jvm-poc/interpreter.cpp#L1190-L1203):

```cpp
auto objectIt = rt.heap.find(*obj);
if (objectIt == rt.heap.end()) return;
const HeapObject& ho = objectIt->second;
if (ho.cls != nullptr) {
    auto it = rt.fieldRefSlots.find(ho.cls);
    if (it != rt.fieldRefSlots.end()) {
        for (uint16_t s : it->second) {
            if (s < ho.fields.size())
                markValue(Value::ofInt(ho.fields[s]), rt, markedObjects, markedArrays);
        }
    }
}
```

- A ref word **is** the tagged handle int, so `Value::ofInt(word)` reconstructs
  a `kInt` value and the existing `objectId`/`arrayId` decode works unchanged —
  `markValue` stays `Value`-based; only the field-iteration loop changes.
- This is **strictly more correct**: it eliminates today's false-positive risk
  (a plain `int` field equal to `0x01000005` is traced as obj#5 right now), and
  it's **faster** (walks only ref slots, not every field).
- `fieldRefSlots` must be populated for any class whose objects can be live at a
  GC — i.e. populated in `buildFieldSlots`, which already runs on object
  allocation ([allocateObject:643](../tools/jvm-poc/interpreter.cpp#L643)), so
  the list exists before any instance can be reachable.

**Out of scope for GC:** arrays. Primitive arrays are `CompactArrayHeap`
(`vector<int32_t>`, no refs); object arrays are `ArrayHeap` (`vector<Value>`,
still tagged). [markValue:1214](../tools/jvm-poc/interpreter.cpp#L1214)'s
array-element loop is unchanged. Only `HeapObject::fields` is being compacted.

## 7. Edge cases & correctness

- **Inheritance slot stability.** Subclass fields append after parent fields
  (preserved). The cached `(cls, cpIdx) → slot` mapping stays valid because a
  field declared in class C sits at the same word index in C and all
  subclasses — same invariant the current cache already relies on.
- **`FieldSlot` + bare-name fallback.** The bare-name entry
  ([buildFieldSlots:540](../tools/jvm-poc/interpreter.cpp#L540)) must store the
  same `FieldSlot` (same `wordIndex` *and* `kind`) so native name-based access
  reconstructs long fields correctly.
- **`kStr` in fields — AUDIT.** Plan assumes fields are only int/handle/long.
  If any native `writeField` path can store a `kStr` (debug string) into a
  field, this change would silently drop it. Grep `writeField`/`setField`/
  `ctx.writeField` callers before implementing; if found, decide whether those
  are real field stores or sentinels.
- **Endianness** of the long split must be identical in get and put (§3.1).
- **Zero-default** replaces `kNone`. Confirm no code distinguishes
  "uninitialized field" from "field holding 0" (getfield/readField already
  collapse both to 0 today, so this is safe).

## 8. Validation

1. **Correctness first.** Run the C++ test suite (`make -C tools/jvm-poc test`,
   esp. inherited-field-lookup and gc-roots). **Add a regression test**: an
   object with a `long` field whose bytes alias a handle pattern
   (`0x01000005_00000001`) plus a real reference field, run a GC, assert the
   real ref survives and the long is *not* traced as an object.
2. **Performance second, clean.** Profiling **off**, game2, steady state,
   compare min/avg/max fps and frame max against the pre-change baseline
   (avg 16.0 / min 9.2 / max 108.5 ms). Do **not** trust any cyc-bracketed
   sub-region number — that mistake is documented twice in
   [docs/rejected/](rejected/). The footprint win is independent and can be
   confirmed from heap accounting regardless of the speed result.

## 9. Step ordering (suggested)

1. Introduce `FieldKind`/`FieldSlot`, change `fieldSlotCache`/`fieldIndexCache`
   value type to `FieldSlot`, and add `fieldRefSlots` + slot-width in
   `buildFieldSlots` (no behavior change yet — `fields` still `vector<Value>`,
   indexed by `FieldSlot::wordIndex`; just compute the extra metadata and assert
   it agrees with current slots for non-long classes).
2. Flip `HeapObject::fields` to `vector<int32_t>` and update the 5 access sites
   (§5) together — it won't compile until all are done; do it in one pass.
3. Rewrite `markValue` object branch (§6) + add the GC regression test.
4. Build, full test suite, fix.
5. Clean profiling-off A/B (§8). If neutral/positive **and** footprint drops,
   keep; record result. If it regresses, revert and write a
   `docs/rejected/` entry with the why.

## 10. Open questions

- **DECIDED: use the `FieldSlot` struct** (§3 "Slot-cache value type"), not
  bit-packing into the `uint16_t`. Rationale: it costs nothing (node padding
  absorbs it — §1.1), reads clearer, and gives a typed extension point for
  future per-kind handling. `kind` carries only `kWord`/`kLong` for now; no
  other kinds are needed yet.
  Reminder: `FieldSlot` is the **cache value** (one per call-site, returned by
  the `(cls,cpIdx)→slot` lookup), not the field element. `wordIndex` is the
  *position* in `HeapObject::fields`, never the 4-byte field value — the stored
  element is always a bare `int32_t`. Putting a `kind`/tag on the element itself
  would make it 5 bytes → 8 aligned and erase the entire compaction; that is
  exactly why kind lives in the cache (`FieldSlot`) and ref-ness in
  `fieldRefSlots` (GC), not alongside each stored word.
- Do we ever need a *richer* `kind` (ref vs int vs float) on the hot path?
  getfield value reconstruction only needs `kWord` vs `kLong` (ref/int/float
  all reconstruct as `ofInt`); ref-ness is GC-only via `fieldRefSlots`. Current
  answer: the two-value `kind` is sufficient for the hot path — but `FieldKind`
  is an enum precisely so a future need (e.g. a narrowed-int fast path) can add
  a value without touching the cache shape.
