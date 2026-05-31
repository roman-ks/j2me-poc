# Per-Class Call-Site Cache (Method Dispatch Refactor)

## Goal

Replace the global `unordered_map<uint64_t, ResolvedCallEntry> callCache` with a
per-`ClassFile` array indexed directly by constant-pool index, and eliminate
`MethodRef` construction from the native-call hot path. The same array serves
both native and Java method dispatch.

Expected payoff: ~5–15% per-frame on representative scenes; larger on ESP32.

## Data-driven sizing

Measured across this project (103 classes, see `tools/cp_methodref_count.py`):

- Max method-ref CP index seen per class: 30 (boot), 20 (game)
- Avg method-refs per class: 4.7
- Total method-refs: 419

### Layout: indirection table + packed sites

Storage is split in two:

- `cpToSite[cpIdx] → uint16_t siteIdx` — sized to `max_methodref_cpIdx + 1`.
  `0xFFFF` sentinel means "this CP slot is not a method-ref".
- `sites[siteIdx] → CallSiteEntry` — packed, one entry per Methodref /
  InterfaceMethodref CP entry. No holes.

Rejected alternatives:

| Layout | Memory (this project) | Lookup | Worst-case waste |
|---|---|---|---|
| A. `sites[cpIdx]` sized to `max_methodref_cpIdx+1` | ~28 KB (7 KB waste) | 1 indexed read | unbounded (scattered methodrefs) |
| B. Packed sorted + binary search | ~21 KB | log₂(N) compares + unpredictable branches | none |
| **C. cpToSite indirection (chosen)** | ~22 KB | 2 indexed reads (warm-cache: free) | none |

Why C beats A: bounded by definition (storage is O(actual_methodref_count +
max_methodref_cpIdx)). A class with scattered methodrefs at cpIdx {10,50,100,150,200}
costs ~8 KB under A vs ~410 B under C.

Why C beats B: same memory profile, no log factor, no branch-predictor pressure
(important on Xtensa LX7). The `cpToSite` table is one cache line (avg ~14 B,
max ~60 B per class); after the first dispatch for a class, both arrays sit in
L1, second indirection is free.

## Data structures

### CallSiteEntry

```cpp
struct CallSiteEntry {
    enum Kind : uint8_t { kUncached = 0, kNative = 1, kJava = 2, kNotFound = 3 };

    Kind kind = kUncached;
    uint8_t argSlots = 0;             // does NOT include 'this' for virtual

    // --- Native path ---
    NativeLeafFn nativeFn = nullptr;  // 8B, resolved leaf (no MethodRef)

    // --- Java path ---
    const ClassFile* targetClass = nullptr;   // 8B
    const MethodInfo* targetMethod = nullptr; // 8B

    // --- Virtual-only PIC slot (used by 0xb6/0xb9, ignored by static/special) ---
    const ClassFile* receiverClass = nullptr; // 8B, runtime class this entry is valid for
};  // ~40B with padding
```

Storage lives on `ClassFile`:

```cpp
struct ClassFile {
    // ...existing fields...

    // CP index → packed site index. Sized to (maxMethodRefCpIdx + 1).
    // 0xFFFF sentinel = this CP slot is not a method-ref; a well-formed
    // invokeX bytecode never queries such a slot, but the sentinel keeps the
    // hot path defensive against malformed input.
    std::vector<uint16_t> cpToSite;

    // Packed: one CallSiteEntry per Methodref / InterfaceMethodref CP entry.
    // Filled at class-load (kUncached) and populated lazily on first call.
    mutable std::vector<CallSiteEntry> sites;
};
```

`sites` is `mutable` because resolution happens lazily on first call.
`cpToSite` is fixed at class-load and immutable.

### New leaf signature

```cpp
using NativeLeafFn = NativeCallResult(*)(
    NativeCallContext& ctx,
    uint32_t pc,
    const std::vector<Value>& args);
```

Dropped from the hot path: `methodLabel` (derived from frame, only used for
trace records), `MethodRef` (3 std::string copies — the main win). Handlers
that genuinely need them keep the old `NativeHandler` signature as a "thick
leaf" variant, called from a small forwarding wrapper.

### Resolver (replaces `resolveNativeStaticHandler` / `resolveNativeInstanceHandler`)

```cpp
// Returns the leaf for (className, methodName, descriptor) on the static or
// instance path. nullptr means "no leaf — slow cascade in handleNativeXCall
// must run (e.g. handleString fired by receiver-type, not className)."
NativeLeafFn resolveStaticLeaf(const std::string& className,
                               const std::string& methodName,
                               const std::string& descriptor);
NativeLeafFn resolveInstanceLeaf(const std::string& className,
                                 const std::string& methodName,
                                 const std::string& descriptor);
```

Each native_methods/*.cpp file owns a `NativeMethodEntry`-style table the
resolver consults — small linear scan over ~5-30 methods per class, run once
per call site per class, never on the hot path.

**Strings on hot vs slow path** — explicit invariant:

| Path | Frequency | Touches strings? |
|---|---|---|
| Hot path (cache hit) | every call | **no** |
| First call per site (`kUncached`) | once per call site, ever | yes (resolver) |
| Virtual PIC miss | rare, per site | yes (resolver) |
| Class load | once per class at boot | yes (CP parse) |

Strings on the resolver signature are unavoidable here because the input is
the methodref CP entry, which is `(className, name, descriptor)` strings by
.class file spec. They cost nothing on the hot path. See
[Post-implementation considerations](#post-implementation-considerations) for
the eager-resolution variant that drops them from runtime entirely.

## File-by-file changes

| File | Change |
|---|---|
| `class_file.hpp` | Add `CallSiteEntry`, `NativeLeafFn` typedef, `cpToSite` + `sites` vectors on `ClassFile`. |
| `class_file.cpp` | After CP parse, single pass to: (a) compute `maxMethodRefCpIdx`, (b) resize `cpToSite` to that+1 filled with `0xFFFF`, (c) for each Methodref/InterfaceMethodref entry, push a `kUncached` `CallSiteEntry` and record its packed index in `cpToSite[cpIdx]`. |
| `native_methods.hpp` | Add `NativeLeafFn` typedef, `resolveStaticLeaf`/`resolveInstanceLeaf` decls. Keep `NativeHandler` + `handleNativeStaticCall`/`handleNativeInstanceCall` for slow cascade. |
| `native_methods/dispatch.cpp` | Add the two `resolveXLeaf` functions; route by `(className, name, descriptor)`. Keep existing `handleNativeXCall` for slow cascade. |
| `native_methods/handlers.hpp` | Add a `NativeMethodEntry { name, descriptor, NativeLeafFn fn }` table per class, exported as `kImageMethods[]`, `kGraphicsMethods[]`, etc. The `handleX` thick handlers remain for slow cascade and use the same table. |
| `native_methods/*.cpp` | Each class file gets its method table at the bottom; leaf functions converted to new signature one at a time. |
| `interpreter.cpp` (invokestatic 0xb8) | New fast path indexes `cls.callSites[cpIdx]`; on `kUncached` runs current slow path and fills the entry. Drops `MethodRef nativeRef{...}` from the leaf-call path. |
| `interpreter.cpp` (invokevirtual/special 0xb6/0xb7/0xb9) | Same shape, plus receiver-class check (see below). |

## Hot-path dispatch (invokestatic 0xb8)

```cpp
const uint16_t cpIdx = codeU2(code, pc + 1);
if (cpIdx < cls.cpToSite.size()) {
    const uint16_t siteIdx = cls.cpToSite[cpIdx];
    if (siteIdx != 0xFFFF) {
        auto& site = cls.sites[siteIdx];
        if (site.kind == CallSiteEntry::kNative && site.nativeFn) {
            // FAST PATH — no hash, no MethodRef, no string ops
            rt.callArgsBuf.resize(site.argSlots);
            for (size_t i = site.argSlots; i > 0; --i) rt.callArgsBuf[i-1] = frame.pop();
            auto result = site.nativeFn(sharedNativeCtx, callPc, rt.callArgsBuf);
            // ...handle result identical to today...
            pc += 3; break;
        }
        if (site.kind == CallSiteEntry::kJava) {
            // FAST PATH — pushJavaFrame directly
            rt.callArgsBuf.resize(site.argSlots);
            for (size_t i = site.argSlots; i > 0; --i) rt.callArgsBuf[i-1] = frame.pop();
            runtimeFrame.lastCallPc = callPc;
            runtimeFrame.pc = pc + 3;
            pushJavaFrame(rt, classes, *site.targetClass, *site.targetMethod, rt.callArgsBuf);
            return std::nullopt;
        }
        // site.kind == kUncached → fall through to slow path
    }
}
// SLOW PATH — resolve and fill site (today's logic + leaf resolution)
```

For invokevirtual/invokeinterface, the inner `if (site.kind == kNative ...)`
branches also check `site.receiverClass == receiver.cls` (see virtual section
below).

## Migration order

Land in this sequence so each commit is independently testable and revertable:

1. **Scaffolding only**: add `CallSiteEntry`/`NativeLeafFn`/`callSites` types and per-class sizing. Don't use them yet. Verify build + tests pass.
2. **invokestatic native fast path**: convert 0xb8 native-call path. Leaf resolver returns nullptr for everything → falls through to existing slow path. Verify no regressions.
3. **First leaf conversion (e.g. Image)**: convert `image.cpp` handlers to new signature, populate `kImageMethods[]`, wire into `resolveStaticLeaf`/`resolveInstanceLeaf`. Other classes still on slow path.
4. **invokestatic Java fast path**: convert Java method invocation in 0xb8.
5. **invokevirtual/special fast path with receiver check**: convert 0xb6/0xb7/0xb9 (see virtual section below).
6. **Roll out remaining leaf conversions**: graphics, canvas, font, display, system, thread, midlet, rms, native_runtime, media. One per commit.
7. **Retire `callCache`**: once all paths use `callSites`, delete `callCache`, `callCacheKey`, `ResolvedCallEntry`.
8. **Drop `MethodRef` from slow cascade**: only after step 7. The slow cascade can rebuild `MethodRef` locally if needed for `recordUnknownCall`/trace records.

Each step is mergeable on its own. Steps 2 and 4 are pure-perf; steps 3 and 6
are mechanical handler rewrites; step 5 is the only architecturally tricky one.

---

## ★ Section to expand later: Virtual call polymorphism (0xb6/0xb9)

For invokestatic/invokespecial the target is determined by the call site alone
— monomorphic by definition. For invokevirtual it depends on the runtime
receiver type. We exploit the empirical fact that in J2ME game code, call
sites are **overwhelmingly monomorphic**: the same receiver class hits the
same site repeatedly.

### Single-class inline cache (1-deep PIC)

The `receiverClass` field on `CallSiteEntry` is the inline cache:

```cpp
if (cpIdx < cls.cpToSite.size()) {
    const uint16_t siteIdx = cls.cpToSite[cpIdx];
    if (siteIdx != 0xFFFF) {
        auto& site = cls.sites[siteIdx];
        const ClassFile* recvCls = receiver.cls;  // already cached on HeapObject today

        if (site.kind != kUncached && site.receiverClass == recvCls) {
            // HIT — dispatch directly
            if (site.kind == kNative) site.nativeFn(...);
            else                      pushJavaFrame(*site.targetClass, *site.targetMethod, ...);
        } else {
            // MISS — resolve via findMethodInHierarchy(recvCls, ref.name, ref.descriptor),
            // overwrite site fields, retry. Equivalent to today's cache miss path.
        }
    }
}
```

### Why this is correct

- `receiver.cls` is a `const ClassFile*` already stored on `HeapObject` (see
  `interpreter.cpp:226`). Pointer compare, no string op.
- `findMethodInHierarchy` walks superclasses, so the resolved
  `(targetClass, targetMethod)` is whatever the JVM spec mandates for
  invokevirtual on this receiver.
- On polymorphism (rare): the site overwrites with the new receiver class.
  This is a "megamorphic" pattern of cache thrashing, but since J2ME games
  almost never polymorphically dispatch, the cost is bounded.

### Open questions to refine

- **2-deep PIC vs 1-deep**: should we store a second `(receiverClass, fn)` pair
  to avoid thrash when two classes alternate at one site? Cheap (~16 extra
  bytes/entry), but adds branch on every dispatch. Skip until measured.
- **Megamorphic fallback**: after N misses, set `site.kind = kUncached` permanently
  and route to slow path forever? Probably overkill given empirical
  monomorphism, but easy to add.
- **invokespecial (0xb7)**: not virtual — receiver class is fixed by the
  bytecode (private/super/init). Treat like invokestatic: no `receiverClass`
  check needed. The opcode dispatch needs to branch on `op` to skip the check.
- **invokeinterface (0xb9)**: virtual dispatch on receiver class, same as
  0xb6. The opcode has 2 extra bytes (count + 0) that need to be respected
  (`pc += 5` not `pc += 3`).
- **null receiver**: NPE path unchanged — still happens before cache check
  because cache lookup requires `receiver.cls`.
- **Array receivers**: arrays don't have a `ClassFile*`. Today's code routes
  these to `handleArray`-style logic. The cache check `receiver.cls == nullptr`
  → fall through to slow path. Document this as expected.

### Anti-pattern to avoid

Don't make the receiver check `receiver.className == site.runtimeClassName`
(string compare) — that defeats the whole purpose. Use pointer equality on
`ClassFile*` only. If `HeapObject.cls` is null in some old code path, fix
that path to populate it rather than fall back to string comparison.

---

## ★ Section to expand later: Slow cascade strategy

Today's `handleNativeInstanceCall` (`dispatch.cpp:77-143`) routes by
`ref.className` (static type at the bytecode level), with three special-case
receiver-type fallbacks for cases where the static type is an abstract
parent (`Object`, `Displayable`) but the runtime type is more specific:

```cpp
// dispatch.cpp:109   — Image fallback
if (ref.className == "javax/microedition/lcdui/Image" || imageId(receiver).has_value()) {
    return handleImage(...);
}

// dispatch.cpp:126   — String fallback
if (ref.className == "java/lang/String" || isStringObject(ctx, receiver)) {
    return handleString(...);
}

// dispatch.cpp:117-123 — Canvas.getWidth/getHeight subclass fallback
const bool receiverIsCanvas = isClassOrSubclassOf(ctx.receiverClassName, "Canvas");
if (... || (receiverIsCanvas && (ref.name == "getWidth" || ref.name == "getHeight"))) {
    return handleCanvas(...);
}
```

These fallbacks exist because the current dispatcher routes by **static**
class. Proper virtual dispatch through `receiver.cls` makes most of them
disappear.

### Under the new design: only Image needs the cascade

The new PIC-based dispatch resolves through `receiver.cls`:

```cpp
const ClassFile* recvCls = receiver.cls;
const MethodInfo* target = findMethodInHierarchy(
    classes, *recvCls, ref.name, ref.descriptor);
// hierarchy walk finds String.toString() before Object.toString()
// → resolves to nm_string_toString, stored in PIC
```

After the first call, the PIC stores `(receiverClass=String*, fn=nm_string_toString)`
and pointer-compares on subsequent calls. No string check ever runs on the
hot path.

Breakdown by type:

| Receiver type | Has `HeapObject.cls`? | Slow cascade still needed? |
|---|---|---|
| String | yes (`String*`) | **no** — PIC subsumes `isStringObject` via virtual dispatch |
| Canvas / GameCanvas subclass | yes (subclass `ClassFile*`) | **no** — PIC subsumes `getWidth`/`getHeight` quirk |
| Image | **no** (tagged H1 handle into `ctx.images`, no `HeapObject`) | **yes** — `receiver.cls` is unavailable for image handles |
| All className-exact (Graphics, Display, Font, Midlet, Thread, RecordStore, Player, NativeRuntime) | yes | no (fast path handles them) |

**The slow cascade collapses to just Image.** String and Canvas fold into
normal virtual dispatch via the PIC, and the className-exact handlers are
all cacheable by definition.

### Image — why it stays in the slow cascade

Image receivers are H1 handles tagged `kHandleImgTag` (see `helpers.cpp:46`),
keyed into `ctx.images`. They are not `HeapObject`s and have no
`ClassFile*` — so the PIC's `site.receiverClass == receiver.cls`
pointer-compare can't be applied. The check `imageId(receiver).has_value()`
is doing work the PIC fundamentally can't replicate without restructuring
image storage.

This leaves two options for Image (defer the choice until the rest is
landed):

- **Keep the slow cascade for Image** — small fixed cost, only fires when
  a call site's static class isn't `javax/microedition/lcdui/Image`. Most
  Image calls have a literal `Image` static type at the call site, so the
  fast path covers them; the cascade is for the `Object.toString()` style
  edge cases on image receivers (which are rare).
- **Promote Image to a `HeapObject` with a synthetic Image ClassFile** —
  bigger structural change, but unifies dispatch. `ctx.images` would still
  exist as the storage backend, but the handle would carry a `cls` pointer.
  Probably not worth doing just for dispatch uniformity.

For now: keep slow cascade, scope it to Image only.

### In scope for this work

- **String**: drop the `isStringObject` fallback, fold into PIC via virtual
  dispatch through `receiver.cls`. Validate by running the test suite and
  checking that `Object.toString()`/`Object.equals()` on String receivers
  still resolves correctly through `findMethodInHierarchy`.
- **Canvas `getWidth`/`getHeight`**: drop the subclass-of-Canvas quirk,
  fold into PIC. The subclass walk happens once at slow-path resolution
  inside `findMethodInHierarchy`, then PIC takes over.
- **Slow cascade shrinks to Image-only**: `handleNativeInstanceCall` keeps
  the `imageId(receiver)` branch and drops everything else. The
  className-exact branches (Graphics, Display, Font, Midlet, Thread, RMS,
  Player, NativeRuntime) all go away once the fast path resolves them.

### Out of scope for this work

- Promoting Image to `HeapObject`.
- Generalizing the PIC to handle non-`HeapObject` receiver types
  (image/array/graphics-context handles).

### Open questions to resolve during implementation

- **Tagging "this site needs the cascade"**: simplest is a `kSlowCascade`
  variant of `CallSiteEntry::Kind` so the fast path treats it as "always
  miss" and skips straight to `handleNativeInstanceCall`. Avoids
  re-resolving on every call to an Image-on-Object site.
- **`isClassOrSubclassOf` cost during resolution**: `findMethodInHierarchy`
  already walks superclasses, so this work is already paid for once at PIC
  population. Confirm there's no extra cost.
- **What if `receiver.cls` is null on a non-image HeapObject?**: this would
  be a bug elsewhere. Add a debug assert at PIC-populate time; in release
  fall through to cascade.

---

## Risks

1. **ClassFile pointer stability**: classes live in `std::vector<ClassFile>`
   in `Runtime.classes`. A `push_back` after dispatch begins invalidates all
   stored `ClassFile*`. The existing `callCache` already depends on this not
   happening. Document the invariant explicitly and add a debug assertion
   that `classes.capacity()` doesn't change after `jvm_midlet_app::start()`.

2. **`HeapObject.cls` must be populated**: virtual dispatch relies on
   `receiver.cls` being a valid `ClassFile*`. Today this is set at object
   allocation (`interpreter.cpp:226`). Any code path that constructs a
   `HeapObject` and forgets to set `cls` would silently fall to the slow
   path; verify all `new`/`newarray`/`anewarray` paths populate it.

3. **Mutable cache on `const ClassFile`**: the `callSites` vector is
   `mutable` so that const-correct iteration over classes can still trigger
   resolution. This is standard C++ inline-cache practice but worth a one-
   liner in `class_file.hpp` explaining why.

4. **Memory budget on ESP32**: ~22 KB extra heap (sum of `cpToSite` +
   `sites` across all classes). Goes into PSRAM (vector default allocator).
   Acceptable. `cpToSite` tables are small enough (~14 B avg per class) that
   placing the active set in SRAM could be considered later if profiling
   shows L1 pressure, but not in the initial implementation.

5. **Backward compatibility during migration**: until all handlers are
   converted, both leaf signatures coexist. Keep the old `NativeHandler`
   typedef and `handleX` thick handlers untouched; new `NativeLeafFn` adds
   alongside. Once migration completes, the thick handlers retire.

## Validation

- **Correctness**: full test suite must pass at each migration step
  (`make -C tools/jvm-poc test`).
- **Performance**:
  - Baseline: capture `task_invoke=` and per-frame totals for 3 representative
    games on current main.
  - After each step: same measurement, expect monotonic improvement (or no
    regression for scaffolding steps).
  - If a step regresses: log into `docs/per-class-call-site-cache-options.md`
    with the cache shape and the why (per CLAUDE.md "Optimisation Discipline").
- **Memory**: log `sum(cls.cpToSite.size() * 2 + cls.sites.size() * sizeof(CallSiteEntry))`
  at startup on ESP32, confirm <50 KB.
- **Cache hit rate**: add a counter on the slow path; expect >99% hit rate after
  warmup for the common game frame.

## Post-implementation considerations

Deferred until after the initial implementation is measured. Do not bundle
into the initial work — the point of staging is to see results before
expanding scope.

### Eager resolution at class-load (drop strings from runtime entirely)

The initial implementation resolves leaves lazily: first call to a site runs
the string-keyed resolver, fills `sites[i]`, subsequent calls skip strings.
This keeps strings on the slow path (~once per call site), but they still
exist at runtime.

Eager variant: walk all methodrefs at class-load time and populate
`sites[i].nativeFn` immediately for native targets. Then:

- Runtime invariant: after `jvm_midlet_app::start()`, no native dispatch path
  touches strings. Ever.
- `kUncached` only remains for Java methods whose target class wasn't loaded
  yet at the time the referencing class was parsed.

Requires a two-pass class loader:
1. Parse all .class files into `Runtime.classes` (build `cpToSite`, allocate
   `sites` as `kUncached`).
2. After all classes loaded, walk each class's methodrefs and resolve native
   targets. Java methods stay `kUncached` and use the lazy path.

Defer until the lazy variant is measured. Worth doing if:
- Profiling shows class-load → first-frame transition has visible string-cost spikes
- Or we want the cleaner invariant for debugging / static analysis

### Internal resolver: drop string compares too

Even on the slow path, the resolver currently does
`if (className == "...") if (name == "...") ...` chains. Could be replaced
with hashed-string lookup (FNV/xxhash precomputed at startup, integer
compares at runtime). Slow path runs ~500 times total at warmup, so this is
microscopic savings. Only worth doing if profiling shows class-load is a
visible cost on ESP32. Likely never needed.
