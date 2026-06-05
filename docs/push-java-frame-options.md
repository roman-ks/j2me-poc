# pushJavaFrame options

Attempts to cut the cost of `pushJavaFrame` — the function that allocates a
Java call frame in the arena, copies args into locals, and pushes onto
`rt.callStack`. Hit on every Java-to-Java `invokevirtual`/`invokespecial`/
`invokeinterface`/`invokestatic`, so total cost scales with call density
(640+ calls per slow frame in game2).

Entries follow the template at the bottom of this file. Add new ones above
the template.

---

## Cache MethodInfo::label + argSlotWidths at class load

**Date:** 2026-06-05
**Status:** Shipped. Slow-frame pushJavaFrame **56ms → 5ms (~11×)**. 0xb7
per-call cycles 25,500 → 4,200. min fps 5.9 → 7.6, avg 13.0 → 14.5.

### Hypothesis
At ~640 pushes/slow frame, the per-call work inside `pushJavaFrame` was
~85µs each (~20,000 cycles on Xtensa LX7). Way more than a frame-allocate
should be. Suspected hot allocations: `methodLabel()` string concat
(`Class.name(desc)` → 3 string allocs) and `argumentSlotWidths()` (vector
alloc + descriptor reparse per call).

### Change
- Added `MethodInfo::label` (pre-built `Class.name(descriptor)` string) and
  `MethodInfo::argSlotWidths` (parsed widths) populated by a single
  `populateMethodInfoCaches(ClassFile&)` call at class load.
- `pushJavaFrame` reads `method.label` (one string copy, no concat).
- `initializeFrameArgs` reads `method.argSlotWidths` by reference, drops
  the per-call vector alloc + descriptor parse.
- Follow-up: `RuntimeFrame::label` changed from `std::string` to
  `const std::string*` pointing into `MethodInfo::label` — removed the
  remaining per-push string copy (~1ms/slow frame on top).
- All write sites for the per-frame CCOUNT / opcode counters gated on
  `host->profileFrameTimings` so production builds pay zero overhead.

### Measurement

| metric | before | after |
|---|---|---|
| pushJavaFrame / slow frame | 56-58ms (~32% of frame) | 4.9ms (~5%) |
| 0xb7 cycles / call | 25,500 | 4,200 |
| 0xb7 total / slow frame | 59ms | 10ms |
| avg fps (60-window) | 13.0 | 14.5 |
| min fps | 5.9 | 7.6 |
| frame avg | ~77ms | ~69ms |
| frame max | ~169ms | ~131ms |

### Root cause
`methodLabel()` was 3 PSRAM string allocs per call. `argumentSlotWidths()`
was 1 PSRAM vector alloc + a char-by-char descriptor parse per call. Both
trivially memoizable as pure functions of MethodInfo. Moving them to
class-load time turned per-call heap churn into a one-time ~100KB static
cost — well within the PSRAM budget.

The remaining `RuntimeFrame::label` string copy survived the first pass
because we left the type as `std::string`. Switching to `const std::string*`
killed the last alloc and reclaimed ~1ms/slow frame for almost no code
change (5 use-site dereferences).

### Lesson
1. **Don't recompute pure-of-input data on the hot path.** Method dispatch
   touches MethodInfo on every call; anything derived only from MethodInfo
   should live on MethodInfo, computed once at class load. Cost is
   per-class, paid once; benefit is per-call, paid forever.
2. **If a struct member is a stable string that lives elsewhere, store a
   pointer not a copy.** Copies of >SBO strings (>15-22 chars) are
   per-call PSRAM allocs. For frame labels, every Java method name plus
   descriptor easily exceeds SBO.
3. **Per-bytecode instrumentation must be cheap when off.** The
   opcode-histogram and per-opcode-cycle counters that surfaced this
   bottleneck got built unconditionally first; once they confirmed the
   issue, they were gated on `host->profileFrameTimings` (runtime, branch-
   predictable) so production overhead is one branch per opcode.

### Trap: dual class parsers
**The most expensive bug in this work.** The JVM library has
`parseClassFile()` in `tools/jvm-poc/class_file.cpp`. The ESP32 project has
a second parser, `parseClassBytes()` in
`esp-j2me-poc/src/core/app/esp_extracted_midlet.cpp`, that reads classes
from in-memory bytes (different I/O path, same byte format). When the
post-pass cache populator was added to `parseClassFile`, the ESP32 parser
was missed → boot-class MethodInfos had empty caches → game2's
`StringBuffer`-chain string-concat broke on ESP32 only ("/game2/0" instead
of "/game2/<N>.dat"). Linux worked fine (uses the JVM-library parser).

Fix: hoisted the post-pass into a shared `populateMethodInfoCaches(ClassFile&)`
helper in `class_file.cpp/.hpp`. Both parsers call it.

**Future rule:** any post-pass on `ClassFile` that the runtime depends on
must be an exported helper called by every class loader. Don't inline it
into one parser only.

### Related ideas not tried
- **Cache method label on a different anchor.** `MethodRef` (the constant-
  pool view) could carry a label too. Skipped — `MethodRef` is constructed
  ad-hoc from constant pool and isn't a natural cache key.
- **Deduplicate Class+Method labels by interning.** Many methods share the
  same class prefix; an interned string-pool could avoid duplicate storage.
  Memory saving only (~30KB on this game); not worth the complexity vs
  the per-method-owned string.
- **Pre-allocate a pool of RuntimeFrames** to avoid struct construction
  per push. Not tried — `RuntimeFrame` is mostly pointers + a 4-pointer
  `Frame` view; construction is already cheap. The cost we eliminated was
  inside the labels and arg-widths, not in struct construction.
- **Skip pushJavaFrame entirely for "leaf" methods** (small methods with
  no nested calls). Could inline their bytecodes into the caller's frame.
  Significant complexity (need to detect leaf-ness, manage operand stack
  sharing, exception scope). Not tried — diminishing returns after the
  caching wins.

---

## Template — copy this for new entries

```
## <Short, specific name of the attempt>

**Date:** YYYY-MM-DD
**Status:** Shipped | Reverted | Kept (neutral) | Partially kept (…)

### Hypothesis
One paragraph. What did we believe was slow / wrong, and what mechanism
would make it faster / right?

### Change
3–6 bullets. What code actually changed, terse.

### Measurement
Numbers. Before / after. State the noise floor when relevant.

### Root cause
Why the result came out the way it did. Mechanism, not vibes. Cite the
structural reason the change can or can't pay off.

### Lesson
The generalizable principle. One short paragraph. Future-you reads only
this section when deciding whether to try a similar idea.

### Related ideas not tried
Variants that share the same root cause and therefore should not be tried
without new evidence. List explicitly so they don't get re-proposed.
```
