# ESP32-S3 Stack Overflow: C++ Recursion in Java Dispatch

## Diagnosis

**Confirmed match.** The crash log (`esp32.log`) shows:

```
Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (loopTask)
```

Crash site: `RuntimeFrame` constructor at `interpreter.cpp:237` (moving a `std::string` label),
called from inside `executeMethod`'s lambda, inside `delegateMethodExecution`.

Backtrace frames #0–#40 show exactly 10 repetitions of a 4-frame C++ cycle per Java call level:

```
#0  RuntimeFrame constructor — crash
#1  executeMethod lambda (interpreter.cpp:3017)
#2  std::function::operator() inside delegateMethodExecution (method_execution_delegate.cpp:29)
#3  executeMethod (interpreter.cpp:3019)
#4  resumeCurrentMethod (interpreter.cpp:2768)
... repeat ×10 ...
#41 renderSession (interpreter.cpp:3246)
```

**Root cause:** every Java method call translates into a recursive C++ call. The cycle is:

```
resumeCurrentMethod
  → [invoke bytecode] executeMethod(depth+1)
      → delegateMethodExecution(std::function)
          → lambda: push RuntimeFrame, resumeCurrentMethod(depth+1)
```

`rt.callStack` holds the logical Java frames as data (already correct), but the C++ call stack
mirrors them redundantly. 10 Java levels → 40 C++ frames → 32 KB `loopTask` stack exhausted.

The `kMaxCallDepth = 64` guard (interpreter.cpp:41) was never reached because the OS-level
stack canary fires first.

---

## Options

### Option 1 — Trampoline (iterative dispatch)

**What:** Eliminate C++ recursion entirely. `resumeCurrentMethod` signals "invoke this method"
by pushing the new `RuntimeFrame` onto `rt.callStack` and returning a sentinel instead of
recursing. An outer loop in `renderSession`/`startMidletSession` re-enters `resumeCurrentMethod`
on the newly-pushed frame. When a frame returns, the trampoline pushes the return value to the
caller's operand stack.

**Risk:** high (core interpreter change). Needs full test suite pass + ESP32 verification.

**Outcome:** permanently bounded C++ stack regardless of Java call depth. Correct fix.

---

#### 1.1 Core contract after the refactor

`resumeCurrentMethod(classes, rt)` (no `depth` parameter) runs the bytecode loop and stops on exactly one of three events:

| Event | Effect on `rt.callStack` | Return value | Flags |
|-------|--------------------------|--------------|-------|
| Java call needed | size **+1** (new frame pushed) | `std::nullopt` | none |
| Method returns | size **−1** (frame popped via `finish()`) | return value or `nullopt` for void | none |
| Yield / step-limit | size **unchanged** | `std::nullopt` | `rt.yieldRequested = true` |

The **trampoline loop** (new helper `runTrampoline`) drives execution:

```cpp
void runTrampoline(const vector<ClassFile>& classes, Runtime& rt) {
    while (!rt.callStack.empty()) {
        const size_t sizeBefore = rt.callStack.size();
        auto returnVal = resumeCurrentMethod(classes, rt);
        if (rt.yieldRequested || rt.pendingException.has_value()) break;
        const size_t sizeAfter = rt.callStack.size();
        if (sizeAfter >= sizeBefore) continue;          // new frame pushed — keep running
        // Method returned — push its return value to the caller's operand stack
        if (returnVal.has_value() && !rt.callStack.empty())
            rt.callStack.back().frame.push(*returnVal);
    }
}
```

---

#### 1.2 New field on `RuntimeFrame` (interpreter.cpp:237)

```cpp
struct RuntimeFrame {
    std::string label;
    const ClassFile* cls    = nullptr;
    const MethodInfo* method = nullptr;
    size_t pc               = 0;
    size_t lastCallPc       = SIZE_MAX;  // NEW: pc of the invoke that started the pending Java call
    Frame frame;
    std::vector<Value> suspendedSlots;
};
```

`lastCallPc` holds the bytecode offset of the invoke instruction that made the most recent Java
call from this frame. It is the throw-site pc used for exception table lookup when the callee
throws. `SIZE_MAX` means "no pending call" (frame has not yet made any Java call, or the last
call returned normally).

---

#### 1.3 New helper `pushJavaFrame` (replaces the `executeMethod` setup code)

Encapsulates the frame setup currently spread across `executeMethod` (interpreter.cpp:2984–3019).
Returns `false` on depth limit or arena overflow, handling the error inline before returning.

```cpp
// interpreter.cpp — replace executeMethod's body with this helper + runTrampoline calls
bool pushJavaFrame(
    Runtime& rt,
    const vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    vector<Value>& args) {

    if (rt.callStack.size() >= kMaxCallDepth) {
        // Throw StackOverflowError instead of returning a sentinel
        setPendingException(rt, makeError(rt, classes, "java/lang/StackOverflowError"), "", 0);
        return false;
    }
    Value* slabBase = rt.callStack.empty()
        ? rt.frameArena.begin()
        : rt.callStack.back().frame.stackEnd();
    const size_t needed = method.maxLocals + method.maxStack;
    if (slabBase + needed > rt.frameArena.end()) {
        setPendingException(rt, makeError(rt, classes, "java/lang/StackOverflowError"), "", 0);
        return false;
    }
    std::string label = rt.trace.recording ? methodLabel(cls, method) : std::string{};
    RuntimeFrame f{std::move(label), &cls, &method, 0, SIZE_MAX,
        Frame(slabBase, rt.frameArena.end(), method.maxLocals, method.maxStack), {}};
    initializeFrameArgs(f, args);
    rt.callStack.push_back(std::move(f));
    return true;
}
```

---

#### 1.4 Changes inside `resumeCurrentMethod`

**Remove `depth` parameter.** The function signature becomes:

```cpp
std::optional<Value> resumeCurrentMethod(
    const vector<ClassFile>& classes, Runtime& rt);
```

**Entry exception check (NEW block at the top of the function body, before the bytecode loop):**

When re-entering a frame after a Java call threw, the trampoline calls `resumeCurrentMethod` with
`rt.pendingException` set. The entry guard catches or re-throws:

```cpp
if (rt.pendingException.has_value() && runtimeFrame.lastCallPc != SIZE_MAX) {
    const uint32_t throwPc = static_cast<uint32_t>(runtimeFrame.lastCallPc);
    runtimeFrame.lastCallPc = SIZE_MAX;
    if (handlePendingExceptionAt(rt, classes, method, frame, throwPc, pc)) {
        runtimeFrame.pc = pc;   // caught — pc updated to handler; fall through to bytecode loop
    } else {
        return finish(std::nullopt);  // not caught — propagate up
    }
}
```

**At every Java invoke bytecode** (currently two sites: `invokestatic` at line 2560, and
`invokevirtual`/`invokespecial`/`invokeinterface` at line 2767):

Replace:
```cpp
runtimeFrame.pc = pc + invokeLength(op);
std::optional<Value> result = executeMethod(classes, *targetClass, *targetMethod, rt.callArgsBuf, rt, depth+1);
if (rt.yieldRequested) return std::nullopt;
if (rt.pendingException.has_value()) {
    if (catchPendingException(callPc)) { break; }
    runtimeFrame.pc = callPc;
    return finish(std::nullopt);
}
if (result.has_value()) frame.push(*result);
pc += invokeLength(op);
break;
```

With:
```cpp
runtimeFrame.lastCallPc = pc;                   // save invoke pc for exception propagation
runtimeFrame.pc = pc + invokeLength(op);        // save continuation pc
if (!pushJavaFrame(rt, classes, *targetClass, *targetMethod, rt.callArgsBuf)) {
    // pushJavaFrame set rt.pendingException on overflow
    if (catchPendingException(static_cast<uint32_t>(pc))) { break; }
    runtimeFrame.pc = pc;
    return finish(std::nullopt);
}
return std::nullopt;  // signal to trampoline: new frame is on the stack
```

**`invokeDisplayableNotify` lambda** (line 1472) — currently calls `executeMethod(depth+1)`. This
is a void, blocking sub-call triggered from a native handler. Replace with a local blocking loop:

```cpp
auto invokeDisplayableNotify = [&](const Value& displayable, const char* methodName) {
    // ...find owner/method as before...
    std::vector<Value> notifyArgs = {displayable};
    const size_t stackBase = rt.callStack.size();
    if (!pushJavaFrame(rt, classes, *owner, *notify, notifyArgs)) return;
    // run until the pushed frame (and anything it calls) completes
    runTrampolineUntilSize(classes, rt, stackBase);
};
```

`runTrampolineUntilSize` is a variant of `runTrampoline` that stops when
`rt.callStack.size() <= targetSize` (the notify call's frame and all its callees finished).

**`ensureClassInitialized`** (line 1302) — currently calls `executeMethod(depth+1)` for
`<clinit>`. Same pattern as `invokeDisplayableNotify`: `pushJavaFrame` + `runTrampolineUntilSize`.
C++ recursion in `ensureClassInitialized` itself for superclass init is bounded by class
hierarchy depth (≤ 10 in practice) and is not the same problem.

---

#### 1.5 All external `executeMethod(…, rt, 0)` call sites

These become `pushJavaFrame` + `runTrampoline`. The existing trampoline-like loop in
`startSession` (interpreter.cpp:3097–3106) and `resumeTaskStack` lambda (line 3244) are the
same shape; they all converge to `runTrampoline`.

| Location | Current code | New code |
|----------|-------------|----------|
| `startSession` — `<init>` (line 3078) | `(void)executeMethod(…, 0)` | `if (pushJavaFrame(…)) runTrampoline(…)` |
| `startSession` — `startApp` (line 3094) | `executeMethod(…, 0)` + manual resume loop | `pushJavaFrame(…); runTrampoline(…)` |
| `dispatchCanvasKeyEvent` (line 3154) | `(void)executeMethod(…, 0)` | `if (pushJavaFrame(…)) runTrampoline(…)` |
| Task `run()` dispatch (line 3285) | `(void)executeMethod(…, 0)` | `if (pushJavaFrame(…)) runTrampoline(…)` |
| `renderSession` → `paint` (line 3465) | `(void)executeMethod(…, 0)` | `if (pushJavaFrame(…)) runTrampoline(…)` |
| `executeStraightLine` (line 3518) | `(void)executeMethod(…, 0)` | `if (pushJavaFrame(…)) runTrampoline(…)` |
| `resumeTaskStack` lambda (line 3244) | manual `while` loop | `runTrampoline(…)` |

The existing `startApp` resume loop (lines 3097–3106) is already structurally a trampoline; it
collapses into a single `runTrampoline` call.

---

#### 1.6 Remove `delegateMethodExecution`

Delete `method_execution_delegate.cpp` and `method_execution_delegate.hpp`. The only real work
`delegateMethodExecution` did was time `GameScreen.make_buf` on PC — unused, PC-only debug code.
With `executeMethod` gone, there is no longer a call site for it.

---

#### 1.7 `kMaxCallDepth` adjustment

Move the guard from `executeMethod` (C++ depth check) to `pushJavaFrame` (Java logical depth
check via `rt.callStack.size()`). The current value of 64 is appropriate for Java logical depth
(most games stay well under 20 levels). The guard now throws a proper
`java/lang/StackOverflowError` instead of silently returning a sentinel.

---

#### 1.8 Scope estimate

- `interpreter.cpp`: ~250 lines changed (invoke site rewrites, `resumeCurrentMethod` entry guard,
  new helpers, external call site updates, `resumeTaskStack` collapse)
- `method_execution_delegate.cpp` / `.hpp`: deleted (2 files, ~50 lines)
- No changes to `native_methods.*`, `frame.*`, or any other file

---

### Option 2 — Eliminate `delegateMethodExecution` from the hot path

**What:** `delegateMethodExecution` (method_execution_delegate.cpp) only does real work for
`GameScreen.make_buf(II)V` on PC. On ESP32 `shouldTimeMethod` is always false — it is a pure
pass-through. The `std::function` wrapper adds 2 C++ frames per Java level for nothing.

Remove the `delegateMethodExecution` call from `executeMethod`. Keep the `make_buf` timing
inline in `executeMethod` under `#ifdef PC_BUILD`.

**Effect:** 4 C++ frames per Java level → 2 (just `executeMethod` + `resumeCurrentMethod`).
Stack capacity before overflow doubles: ~10 Java levels → ~20.

**Scope:** small, surgical. Affects `executeMethod` and `method_execution_delegate.cpp`.

**Risk:** low. Does not change semantics. The timing probe is PC-only debug code.

**Outcome:** partial improvement; not a permanent fix. Still unbounded C++ recursion.

---

### Option 3 — Increase `ARDUINO_LOOP_STACK_SIZE`

**What:** `platformio.ini` already sets `ARDUINO_LOOP_STACK_SIZE=32768` (32 KB). Increasing to
e.g. 65536 (64 KB) buys more headroom.

**Cost:** 32 KB of SRAM is significant on a 512 KB SRAM budget. Other tasks and the C++ runtime
heap also live in SRAM.

**Risk:** none, one-line change. But it's a band-aid — a different game with deeper call chains
will crash again.

**Outcome:** temporary; does not fix the root cause.

---

### Option 4 — Lower `kMaxCallDepth` to throw before crash

**What:** `kMaxCallDepth = 64` (interpreter.cpp:41) was intended to catch runaway recursion but
fires too late — the OS stack canary triggers at ~10 Java levels (40 C++ frames). Lowering to
e.g. 12 ensures the JVM throws `<call-depth-limit>` before the stack overflows.

**Trade-off:** games with legitimate call chains deeper than 12 levels will break. `<call-depth-limit>`
currently returns a sentinel value, not a Java `StackOverflowError`, so the game may misbehave
silently rather than catch and recover.

**Outcome:** prevents hard crash at the cost of correctness for deeper stacks.

---

## Recommended Approach

**Short-term (quick, low risk):** Option 2 (remove `delegateMethodExecution`) + Option 4 (lower
`kMaxCallDepth` to ~15) together give a crash-safe interim state. Option 2 doubles the usable
depth; Option 4 ensures a controlled failure before the OS-level canary fires.

**Long-term (correct fix):** Option 1 (trampoline). `rt.callStack` already tracks all Java
frames as data — the C++ mirror is the redundancy to eliminate. Exception propagation and yield
already go through `rt.pendingException`/`rt.yieldRequested`; only return-value routing needs
new plumbing (`rt.pendingReturnValue`).

Options 2 and 3 can be done independently and do not block Option 1.
