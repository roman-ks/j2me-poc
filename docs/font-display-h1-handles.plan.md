# Plan: replace Font/Display `kStr` fake-handles with real H1 handles

**Status:** Proposed. Branch `replace-fake-handles` (off game4, no compaction).
**Goal:** represent the `Font` and `Display` singletons as real H1 tagged-int
handles instead of `kStr` sentinel strings (`"font:default"`, `"display#1"`),
so they round-trip through any storage that assumes references are tagged-int
handles.

This is an **independent, behavior-neutral refactor** done *before* the field
compaction (`docs/field-storage-compaction.plan.md`) and rebased under it. The
compaction stores object fields as untagged 32-bit words and recovers
reference-ness from the field descriptor; a `kStr` value stored into a
reference-typed field would be corrupted (it carries a heap `std::string*`, not
a 32-bit handle). Fixing the representation here keeps the two changes
independently testable and revertible — neither should mask a regression in the
other.

## 1. Why this is safe to do on its own

On this branch fields are still `std::vector<Value>` (tagged), so nothing is
broken today. The migration is a pure representation swap; correct programs must
behave identically. The audit below shows the `kStr`-ness is never actually
relied on:

- **No content consumer.** `parseHandle()` (the only `kStr`→id decoder) has
  **zero callers**; nothing parses `"font:default"`/`"display#1"`. No code
  compares these strings.
- **Receiver-agnostic natives.** Every `Font` instance method ignores its
  receiver (one physical font); `Graphics.setFont` is a no-op; `Display`
  methods don't inspect the receiver value (only the trace records it).
- **Dispatch unaffected.** `invokevirtual` on these handles already finds no
  heap object (`objectId(kStr)` → `nullopt`) and dispatches by the *declared*
  class to the native handler. A `0x05`/`0x06` tagged-int also yields
  `objectId == nullopt`, so dispatch is identical.
- **GC unaffected.** `objectId`/`arrayId` only accept `0x01`/`0x02`; both `kStr`
  today and `0x05`/`0x06` tomorrow are ignored by the collector (correct — they
  are singletons, not heap objects).
- **Equality preserved.** `if_acmpeq` compares `kStr` by content and `kInt` by
  value; two identical handles compare equal either way.

The one externally-visible detail: trace output. Four tests assert
`"display#1.setCurrent(obj#2)"`, rendered via `Value::asText()`. `asText` must
keep producing `"display#1"` for the display handle (see §3).

## 2. Encoding

Extend the H1 high-byte scheme (currently `0x01` obj, `0x02` arr, `0x03` image,
`0x04` gfx):

| tag | meaning | singleton value |
|---|---|---|
| `0x05` | Font    | `0x05000001` (`tag \| 1`) |
| `0x06` | Display | `0x06000001` (`tag \| 1`) |

Both are singletons → id `1`. They are tagged-int `Value`s (`Tag::kInt`), so
they store in one 32-bit word and pass through every handle-aware path.

## 3. Changes

1. **`value.hpp`** — add constants beside the existing handle tags:
   ```cpp
   static constexpr int32_t kHandleFontTag    = 0x05 << 24;
   static constexpr int32_t kHandleDisplayTag = 0x06 << 24;
   static constexpr int32_t kFontHandle    = kHandleFontTag    | 1; // singleton
   static constexpr int32_t kDisplayHandle = kHandleDisplayTag | 1; // singleton
   ```
2. **`value.cpp` `asText()`** — add the two cases to the handle switch so traces
   stay readable and the `displayCurrents` tests keep passing:
   ```cpp
   else if (tag32 == kHandleFontTag)    return "font#"    + std::to_string(id);
   else if (tag32 == kHandleDisplayTag) return "display#" + std::to_string(id);
   ```
   `id == 1` → `"display#1"` (satisfies the 4 trace tests) and `"font#1"`.
   *(Note: this changes the font trace string from `font:default` → `font#1`; no
   test or consumer depends on it — grep confirms — but flag it in review.)*
3. **`native_methods/font.cpp`** — `defaultFontRef()` returns
   `Value::ofInt(Value::kFontHandle)` instead of `Value::named("font:default")`.
4. **`native_methods/graphics.cpp:310`** — `nm_graphics_getFont` returns
   `Value::ofInt(Value::kFontHandle)`.
5. **`interpreter.cpp`** — the `Runtime::displayRef` initializer becomes
   `Value::ofInt(Value::kDisplayHandle)` instead of `Value::named("display#1")`.
   `display.cpp` is unchanged (it returns `ctx.displayRef`).

No change required to `if_acmpeq`, method dispatch, GC, `parseHandle`,
`stringArg`, or the field-access paths (per §1).

## 4. Validation

This branch has no compaction, so it is a pure refactor — **any** test-suite
diff is a real regression, not expected churn.

1. `make -C tools/jvm-poc test` — full suite green, paying attention to:
   - the 4 `display currents` assertions (`display#1.setCurrent(...)`),
   - `font dispatch` (`VirtualDispatchTest`),
   - `display notify lifecycle`.
2. Run a real game on the SDL host (text-heavy screen) and confirm fonts render
   and `setCurrent`/repaint still work — the natives are receiver-agnostic, so
   this mainly guards against a missed producer or an `asText`/trace surprise.
3. Grep sanity: after the change, `grep -rn 'named("font\|named("display'`
   returns nothing (all producers migrated).

## 5. How the compaction branch consumes this

Once this lands and the compaction work is rebased on top: a `Font`/`Display`
stored into a reference-typed field is now a real `0x05`/`0x06` handle, so
`writeFieldSlot`/`readFieldSlot` store and recover it as a clean word, and GC's
`fieldRefSlots` walk calls `objectId` on it → `nullopt` → correctly skips it
(singleton, not a heap object). The `FontFieldRoundTrip`/`DisplayFieldRoundTrip`
tests (red on the compaction branch today) go green with no further work.

## 6. Risks / watch-list

- **Missed producer.** Any other site building `named("font…")`/`named("display…")`
  must migrate. Current grep finds only the four producers in §3.
- **`asText` trace drift.** Display must stay `"display#1"`; font changes to
  `"font#1"` (unused, but call it out in review).
- **A consumer that *does* rely on `kStr`.** None found (`parseHandle` dead,
  `setFont` no-op, no content checks). If a future native needs to identify the
  font/display by value, it should match the handle tag, not a string.
- **Step ordering for the rebase.** Land this as its own commit(s) on
  `replace-fake-handles`, verify green, then
  `git rebase --onto replace-fake-handles 46d6dad <compaction-branch>` so the
  compaction commits sit on top.

## 7. Step ordering

1. `value.hpp` constants + `value.cpp` `asText` cases.
2. Migrate the four producers (font.cpp, graphics.cpp, interpreter.cpp displayRef).
3. `make test` green; grep confirms no remaining `named("font/display`.
4. Run a game on SDL to eyeball text + display.
5. Commit. Later: rebase the compaction branch onto this.
