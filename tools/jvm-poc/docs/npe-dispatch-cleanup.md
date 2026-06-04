# Null receiver cleanup after invokevirtual NPE dispatch

After Option A (throw NPE at invokevirtual/invokespecial before method body runs) is implemented,
`args[0]` (the receiver) will never be null when any native handler is entered.

## Why "non-String object calling String methods" is not a real concern

Dispatch is already type-safe for the normal path. `resolveNativeInstanceHandler` is called with
`targetClass->thisClass` — the class where `findMethodInHierarchy` actually *found* the method,
not `ref.className` from the constant pool. Since `String` is final, `toLowerCase` etc. can only
be found there if the receiver's runtime class IS a String. `handleNativeInstanceCall` (which
routes by `ref.className`) is only reached when `resolveNativeInstanceHandler` returned nullptr —
that never happens for the registered classes (String, Canvas, Graphics, …).

The only cases where a non-String receiver reaches `handleString` today:

1. **Null receiver** — `objectId()` returns nullopt → falls to ref.className routing → reaches
   `handleString` if `ref.className == "java/lang/String"`. Fixed by NPE dispatch.
2. **Named value receiver** (`Value::named(...)`) — same failure path, but these only appear when
   something earlier already returned a wrong value; the program is already broken upstream.

After NPE dispatch, all the `strIt == ctx.strings.end()` fallbacks in `handleString` become dead
code for normal operation. They were null guards, not type guards.

---

## Cleanup list

### string.cpp — `caseConvert` lambda (`toLowerCase` / `toUpperCase`)

```cpp
if (strIt == ctx.strings.end()) {
    return handledValue(receiver);   // returns null as if it were a valid String
}
```

**Must change.** This was the active bug: null receiver returned as a valid String, causing the
caller to proceed with a null and eventually loop forever in `indexOf`. After NPE, this branch is
dead, but leaving `return handledValue(receiver)` is still wrong for the named-value edge case.
Change to `return handledValue(Value::ofInt(0))` (return null) so any cascading call on the result
also NPEs cleanly.

---

### string.cpp — `length` named value fallback

```cpp
return handledValue(strIt != ctx.strings.end()
    ? Value::ofInt(static_cast<int32_t>(strIt->second.size()))
    : Value::named("<string-length:" + receiver.asText() + ">"));
```

Was propagating a sentinel so downstream arithmetic didn't silently produce 0. After NPE this
branch is dead for null; only named-value receivers could still trigger it. Simplify to
`Value::ofInt(0)` — returning 0 is no worse than a named sentinel, and avoids the infinite-loop
risk in callers that do `new char[length]`.

---

### string.cpp — remaining `strIt == ctx.strings.end()` guards

`charAt` (returns 0), `indexOf` (returns -1), `compareTo` (returns -1/0), `getChars` (no-op):
all become dead for null receivers. Behaviour for the named-value edge case is still reasonable
(0 / -1 / no-op). No functional change required; the branches can be left as-is or removed for
clarity.

---

## No cleanup needed elsewhere

- `canvas.cpp`, `graphics.cpp`, `display.cpp` — no null receiver checks
- `image.cpp` — `imageId(receiver)` fallback to 0 is still useful for wrong-type receivers
- `midlet.cpp`, `rms.cpp`, `thread.cpp`, `font.cpp`, `system.cpp`, `media.cpp` — no null receiver handling
