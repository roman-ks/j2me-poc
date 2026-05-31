# Native Method Dispatch: Leaf-Handler Caching

## Problem

Every native-method invoke from the bytecode loop currently traverses ≥2 dispatch
layers before reaching the real work:

1. **Fast path (cache hit):** `cachedNativeHandler(...)` → per-class handler
   like `handleGraphics` → linear `if (ref.name == "..." && ref.descriptor == "...")`
   scan through 13 method patterns until match → method body inline.
2. **Slow path (cache miss):** `handleNativeInstanceCall(...)` (className cascade
   in `dispatch.cpp`) → per-class handler → name/descriptor scan → body.

For graphics, the per-class handler `handleGraphics` (graphics.cpp:79) has 13
distinct `if (ref.name == ...)` branches. Each call that reaches the *last*
branch pays 13 string compares + 13 descriptor compares before doing any work.
Graphics calls are on the hot path: every `drawImage`, `setColor`, `translate`,
`fillRect` in every paint cycle hits this scan.

The Java trampoline collapsed redundant C++ call layers by replacing recursion
with a flat loop driven by `rt.callStack` data. The native dispatch equivalent
is to collapse the per-class handler + per-method scan by **caching the leaf
function pointer** at the call site, so the cached path is one indirect call
to the exact method handler with zero string compares.

## Current Architecture

### Resolved-call cache (`interpreter.cpp:209`)

```cpp
struct ResolvedCallEntry {
    uint8_t argSlots;
    bool isNative;
    std::string runtimeClass;
    const ClassFile* runtimeClassPtr = nullptr;
    const ClassFile* targetClass;
    const MethodInfo* method;
    NativeHandler nativeHandler = nullptr;   // per-class handler, or nullptr
};
```

Cache is keyed by `callCacheKey(&cls, cpIdx)` — the caller's class + constant-pool
index — which uniquely identifies a static call site in bytecode.

### Per-class handler signature (`native_methods.hpp:118`)

```cpp
using NativeHandler = NativeCallResult(*)(
    NativeCallContext&, const std::string&, uint32_t,
    const MethodRef&, const std::vector<Value>&);
```

### Receiver-type fallbacks (`dispatch.cpp:109-128`)

Some classes bind dynamically based on receiver type, not static `ref.className`:

- `handleImage` triggered by `imageId(receiver).has_value()` (any class whose
  receiver is an image handle)
- `handleString` triggered by `isStringObject(ctx, receiver)`
- `handleCanvas` triggered by `isClassOrSubclassOf(receiverClassName, ".../Canvas")`

These cannot be cached statically — they need the slow cascade.

`resolveNativeInstanceHandler` documents this: it only returns a non-null handler
when `className` matches one of the known native classes exactly.

## Proposed Approach

### Two-tier resolution

1. **`NativeMethodFn`** — new typedef for leaf-method handlers. Same signature as
   `NativeHandler`; they're interchangeable from the interpreter's perspective.
2. **`NativeMethodEntry`** — `{name, descriptor, fn}` row. One table per native class.
3. **`resolveNativeStaticMethod(className, name, desc)` /
   `resolveNativeInstanceMethod(className, name, desc)`** — new dispatcher functions
   in `dispatch.cpp` that look up the leaf handler. Used by the interpreter at cache
   write time.
4. **`ResolvedCallEntry`** gains `NativeMethodFn leafHandler` alongside the existing
   `NativeHandler nativeHandler`. On cache hit, the interpreter prefers `leafHandler`
   when set; falls back to `nativeHandler` when only the per-class handler resolved.

### Cache lookup logic in the interpreter

```cpp
// On cache miss:
NativeMethodFn leaf = nullptr;
NativeHandler cls  = nullptr;
if (isNativeCall) {
    leaf = isStatic
        ? resolveNativeStaticMethod(targetClass->thisClass, ref.name, ref.descriptor)
        : resolveNativeInstanceMethod(targetClass->thisClass, ref.name, ref.descriptor);
    if (leaf == nullptr) {
        cls = isStatic
            ? resolveNativeStaticHandler(targetClass->thisClass)
            : resolveNativeInstanceHandler(targetClass->thisClass);
    }
}
rt.callCache[ckey] = {..., cls, leaf};

// On cache hit (hot path):
if (cachedLeaf != nullptr) {
    nativeResult = cachedLeaf(ctx, label, pc, ref, args);   // 1 indirect call
} else if (cachedNativeHandler != nullptr) {
    nativeResult = cachedNativeHandler(ctx, label, pc, ref, args);  // class dispatcher
} else {
    nativeResult = handleNativeInstanceCall(...);                   // slow cascade
}
```

### Per-class file structure (template)

```cpp
// graphics.cpp
namespace jvmpoc::native_methods {
namespace {

NativeCallResult nm_drawImage_iii(NativeCallContext& ctx, ...) {
    // exactly the body that used to be inside the matching `if` block
}
NativeCallResult nm_drawImage_region(NativeCallContext& ctx, ...) { ... }
NativeCallResult nm_translate(NativeCallContext& ctx, ...) { ... }
NativeCallResult nm_setColor_int(NativeCallContext& ctx, ...) { ... }
// ... one function per (name, descriptor) pair ...

const NativeMethodEntry kGraphicsMethods[] = {
    {"drawImage", "(Ljavax/microedition/lcdui/Image;III)V",      &nm_drawImage_iii},
    {"drawImage", "(Ljavax/microedition/lcdui/Image;IIIIIIIII)V", &nm_drawImage_region},
    {"translate", "(II)V",                                        &nm_translate},
    {"setColor",  "(I)V",                                         &nm_setColor_int},
    // ...
};

} // namespace

NativeMethodFn resolveGraphicsMethod(const std::string& name, const std::string& desc) {
    for (const auto& e : kGraphicsMethods) {
        if (e.name == name && e.descriptor == desc) return e.fn;
    }
    return nullptr;
}

// Slow-path entry point (still used by the receiver-type cascade in dispatch.cpp).
NativeCallResult handleGraphics(NativeCallContext& ctx,
                                const std::string& label, uint32_t pc,
                                const MethodRef& ref, const std::vector<Value>& args) {
    NativeMethodFn fn = resolveGraphicsMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, label, pc, ref, args);
    return NativeCallResult{};   // unhandled — interpreter records as unknown call
}

} // namespace
```

The table is the **single source of truth** for the class. `handleGraphics` and
`resolveGraphicsMethod` both consume it. Adding a new method means:
1. Write the standalone `nm_xxx` function.
2. Add one row to `kGraphicsMethods`.

No edits to `dispatch.cpp`, no growing `if`-chains.

### `dispatch.cpp` changes

`resolveNativeInstanceMethod` becomes a 2-line cascade per class:

```cpp
NativeMethodFn resolveNativeInstanceMethod(
    const std::string& className,
    const std::string& name,
    const std::string& desc) {
    if (className == "javax/microedition/lcdui/Graphics")  return native_methods::resolveGraphicsMethod(name, desc);
    if (className == "javax/microedition/lcdui/Canvas")    return native_methods::resolveCanvasMethod(name, desc);
    if (className == "javax/microedition/lcdui/game/GameCanvas") return native_methods::resolveCanvasMethod(name, desc);
    if (className == "javax/microedition/lcdui/Image")     return native_methods::resolveImageMethod(name, desc);
    if (className == "javax/microedition/lcdui/Font")      return native_methods::resolveFontMethod(name, desc);
    if (className == "javax/microedition/midlet/MIDlet")   return native_methods::resolveMidletMethod(name, desc);
    if (className == "javax/microedition/lcdui/Display")   return native_methods::resolveDisplayMethod(name, desc);
    if (className == "java/lang/String")                   return native_methods::resolveStringMethod(name, desc);
    if (className == "java/lang/Thread")                   return native_methods::resolveThreadMethod(name, desc);
    if (className == "javax/microedition/rms/RecordStore") return native_methods::resolveRecordStoreMethod(name, desc);
    if (className == "javax/microedition/media/Player")    return native_methods::resolvePlayerMethod(name, desc);
    return nullptr;
}
```

`handleNativeInstanceCall` (slow cascade, line 77) stays exactly as is — the
receiver-type fallback paths (`imageId(receiver)`, `isStringObject`, Canvas
subclass check) still funnel through it for cache-miss cases that can't bind
statically. The leaf cache is a strict win on top.

## What This Buys

### Hot-path cost per native call

| Path | Today | After |
|------|-------|-------|
| Cache hit, common method (first `if` match) | 1 indirect + 1 name compare + 1 descriptor compare | 1 indirect |
| Cache hit, rare method (last `if` of 13) | 1 indirect + ~13 name compares + ~13 descriptor compares | 1 indirect |
| Cache miss | className cascade (~7 string compares) + class dispatch + scan | className cascade → linear table scan + cache write |

For a frame doing 50 `drawImage` + 50 `setColor` + many `translate`/`fillRect`
calls, the saved string compares add up.

### Maintainability

- Method registration is **data**, not code. Adding a new native method is
  table + function — no `if`-chain editing.
- Each native handler becomes a small, focused function — easier to read and
  test than 200-line `if`-cascades.
- Slow path and fast path consume the same table → no risk of one path
  supporting a method the other doesn't.

### Memory cost

- ~150 `NativeMethodEntry` rows across all classes × 24 bytes ≈ 4 KB read-only
  data. Lives in flash/ROM on ESP32, free in PSRAM on host.
- One extra pointer field on `ResolvedCallEntry` (~8 bytes × cache size).

### What this does NOT solve

- Receiver-type dispatch (image-by-id, string-by-receiver, Canvas-subclass) still
  needs the slow cascade. These call sites can't cache the leaf because the
  binding depends on runtime receiver type, not static call-site info.
- Method bodies remain the same — this is purely about dispatch, not the work
  inside each handler.

## Migration Strategy

This refactor can land **one class at a time**. The interpreter cache holds both
`nativeHandler` (per-class) and `leafHandler` (per-method); whichever is non-null
gets called. So:

1. Land the typedef + cache field + interpreter dispatch logic (no behavior change
   yet — `leafHandler` always nullptr).
2. Convert one class (e.g. `graphics.cpp`) to the table pattern. Add its `resolveX`
   to `dispatch.cpp`. That class's call sites now use the leaf cache.
3. Verify (build + tests + SDL demo). If regressions, revert just that file.
4. Repeat for `canvas.cpp`, `image.cpp`, etc., in order of hot-path priority.

This isolates risk per class and lets us measure impact incrementally.

## Files Affected

| File | Change |
|------|--------|
| `native_methods.hpp` | Add `NativeMethodFn` typedef, `NativeMethodEntry` struct, `resolveNativeInstanceMethod` / `resolveNativeStaticMethod` declarations |
| `native_methods/dispatch.cpp` | Add the two `resolveNative*Method` cascades |
| `native_methods/<class>.cpp` (×11) | Extract method bodies into standalone `nm_*` functions; build the `kXxxMethods` table; add `resolveXxxMethod`; rewrite `handleXxx` to dispatch via the table |
| `interpreter.cpp` | Add `NativeMethodFn leafHandler` to `ResolvedCallEntry`; update both invoke sites (static + virtual) to populate and prefer it |

Scope estimate: ~200 lines of new structure (typedef, tables, resolvers), ~150
method bodies relocated (mechanical move from inline `if` block to standalone
function), ~50 lines of interpreter dispatch updates. Per-class refactor cost
~30–50 lines + 5–20 method functions depending on the class.

## Open Questions to Refine Before Implementing

1. **Table lookup strategy**: linear scan vs sorted + binary search vs gperf-style
   perfect hash. Linear scan is fine for ≤15 methods/class (current state). Above
   that, sort by name and binary search. Hash is overkill for these sizes.
2. **Same-name overloads**: `drawImage` has multiple descriptors. The table
   handles this with separate rows; the linear scan compares both name and desc.
3. **Empty-descriptor wildcard?**: currently the slow cascade matches `ref.name`
   ignoring descriptor in some places (e.g. `getWidth`/`getHeight` for Canvas
   subclasses). Whether to support `descriptor = ""` as a wildcard row, or list
   all real descriptors explicitly. Recommend listing all explicit descriptors
   for correctness; the table is the source of truth.
4. **Caching policy for receiver-type fallbacks**: should the cache record a
   "this site uses the slow cascade" sentinel to skip re-resolving on every
   call? Currently `nativeHandler = nullptr` already means that; just confirm
   the interpreter's miss handling does the right thing.
