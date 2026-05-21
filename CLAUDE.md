# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Identity

This is **not** a proof-of-concept — it is a production-grade, performance-first CLDC 1.1 / MIDP 2.0 JVM runtime. The **primary deployment target is ESP32-S3**: a memory- and CPU-constrained microcontroller with ~512 KB SRAM and ~8 MB PSRAM. Linux/SDL2 is a fast-iteration host only. Every change must be written as if it ships directly to ESP32. Never sacrifice correctness or efficiency with "good enough for a POC" reasoning.

## Project Goal

Project targets to be a generic runtime. Optimisations for one particular app cannot be made when sacrificing existing compatibility or performance of other apps. No "overoptimisation" for one particular app.

## Two-Repo Structure

| Repo | Role |
|------|------|
| `j2me-poc` (this repo) | Core JVM runtime, Java boot stubs, MIDlet source, dev tools |
| `esp-j2me-poc` (`/home/roman/projects/esp-j2me-poc`) | PlatformIO host: ESP32 + Linux entry points, build orchestration, HAL |

Active JVM C++ development happens in `tools/jvm-poc/`. The host repo references this directory via `custom_j2me_poc_root` in its `platformio.ini`.

## Build Commands

```bash
# Java MIDlet JAR
mvn package

# Boot classes only
./tools/build-jvm-boot-classes.sh

# JVM C++ runtime (Linux)
make -C tools/jvm-poc

# JVM tests
make -C tools/jvm-poc test

# SDL demo
make -C tools/jvm-poc jvm-poc-sdl-demo

# classdump tool
make -C tools/classdump
```

Maven uses a repo-local `.m2/repository` via `.mvn/maven.config` — never `~/.m2`.

To build and run the full system on ESP32 or Linux host, use the PlatformIO commands in `esp-j2me-poc`.

## Repository Layout

```
src/
  main/                         # Java MIDlet source
  jvm-boot/java/                # Hand-written CLDC/MIDP boot class stubs
tools/
  jvm-poc/                      # C++ JVM runtime (primary development area)
    interpreter.cpp             # Bytecode dispatch engine
    frame.cpp                   # Call frame management
    class_file.cpp              # .class parser
    method_resolution.cpp       # Method lookup
    jvm_midlet_app.cpp          # Public host API
    extracted_midlet.cpp        # MANIFEST.MF / boot class loader
    native_methods/             # Native method handlers
    j2me_port/                  # J2ME platform shims (Canvas, J2MECompat)
    core_shim/                  # ESP32 ↔ x86 portability layer
  classdump/                    # .class inspection tool
docs/                           # Architecture plans and performance findings
*.patch                         # Unapplied git diff snapshots
```

## ESP32 Compatibility Requirements

All C++ code in this repo is compiled for ESP32-S3 via the host repo's PlatformIO build. Write code accordingly:

- **No `std::filesystem`** — use `esp_gallery::Fs` (embedded) or plain `fopen("rb")` (host). `J2MECompat.cpp` is the main enforcer of this rule.
- **No unbounded heap growth** — every allocation that persists across frames must be accounted for against SRAM/PSRAM limits.
- **`#ifdef ESP32_BUILD` / `#ifdef PC_BUILD`** for all platform-specific branches.
- **`SramAllocator`** must be used for hot, short-lived allocations (operand stack, locals) — it targets internal SRAM first for cache-friendly access.
- **`ExecutionTrace`**: recording defaults to `true` on Linux. **Never leave enabled on ESP32 before `app_.start()`** — `BranchTrace`/`LocalWrite` vectors will exhaust PSRAM.
- Diagnostic logging uses `[tag]` prefixes (e.g. `[new]`, `[putfield-hot]`) and is wrapped in `#ifdef ESP32_BUILD`.

## JVM Runtime Architecture (tools/jvm-poc/)

| File | Role |
|------|------|
| `interpreter.cpp` | Bytecode dispatch engine; `kMaxSteps=100000` per frame; all runtime caches in `Runtime` struct |
| `value.hpp` | Flat 12-byte tagged union; references are 24-bit H1 handles, not pointers |
| `frame.cpp` | Per-call locals[] + operand stack[] using `SramAllocator<Value>` |
| `class_file.cpp` | Java 1.3 .class parser: constant pool, exception handler tables |
| `method_resolution.cpp` | `findMethodInHierarchy()` — superclass chain search |
| `jvm_midlet_app.cpp` | Public host API: `loadClasses()`, `start()`, `render()` |
| `extracted_midlet.cpp` | Reads MANIFEST.MF MIDlet-1 entry; appends boot classes |
| `native_methods/dispatch.cpp` | Routes invokestatic/invokevirtual to handler by class name |
| `j2me_port/J2MECompat.cpp` | Resource loading, RecordStore (RMS), J2ME shims |
| `j2me_port/Canvas.cpp` | 2D framebuffer: RGB565, RLE alpha, pluggable image decoder |
| `core_shim/` | ESP32 ↔ x86 portability: log macros, `esp_gallery::Fs`, `SramAllocator` |

### H1 Handle Encoding

References are stored as `Tag::kInt` with the high byte encoding the type:
- `0x01xxxxxx` — object
- `0x02xxxxxx` — array
- `0x03xxxxxx` — image
- `0x04xxxxxx` — graphics context

### Runtime Caches (all in `Runtime` struct)

| Field | Key | Purpose |
|-------|-----|---------|
| `callCache` | `callCacheKey(&cls, cpIdx)` | Resolved method lookup |
| `fieldIndexCache` | `callCacheKey(obj.cls, cpIdx)` | Field name → slot index |
| `fieldSlotCache` | `ClassFile*` | Class → {fieldName → slot} |
| `staticKeyCache` | `callCacheKey(&cls, cpIdx)` | Static field string key |

**`fieldIndexCache` key pairs `obj.cls` (object's class) with cpIdx from the EXECUTING class's constant pool.** Cross-class cache collisions can occur if two executing classes use the same cpIdx for fields on different object types.

### Memory Model

- `SramAllocator`: tries `MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL` (SRAM) first, then falls back
- `HeapObject::fields` uses `std::vector<Value>` (default allocator → goes to PSRAM via `operator new`)
- `newarray` types 4/5/8/9/10 → `CompactArrayHeap` (`std::vector<int32_t>`, 4 bytes/element)
- `anewarray` / object arrays → `ArrayHeap` (`std::vector<Value>`, 12 bytes/element)
- `ExecutionTrace`: recording defaults to `true` — **never leave enabled on ESP32 before `app_.start()`**; `BranchTrace`/`LocalWrite` vectors will exhaust PSRAM

## Platform Differences

| Concern | ESP32 | Linux |
|---------|-------|-------|
| `operator new` | `heap_caps_malloc(PSRAM first, SRAM fallback)` in `esp_main.cpp` | standard |
| Framebuffer | 153,600 B with `MALLOC_CAP_INTERNAL` | SDL2 surface |
| Filesystem root | `/` (SD card) | `./` |
| Trace recording | Must disable before `app_.start()` | enabled by default |
| Timing | `esp_timer_get_time()` | `std::chrono` |
| Build flag | `ESP32_BUILD` | `PC_BUILD` |

## Boot Classes (src/jvm-boot/java/)

Hand-written stubs for `java/lang/`, `java/io/`, `java/util/`, `javax/microedition/`. Key constraint: **avoid bitwise ops** unless the opcode is confirmed supported by the interpreter. Use arithmetic equivalents (e.g. `if (b < 0) b += 256` instead of `b & 255`).

`java/lang/String` constructors funnel through `private native init([CII)V`. GC traces identify string objects by `className == "java/lang/String"`.

## Native Method Pattern

Each handler in `native_methods/` receives `NativeCallContext& ctx` and returns `NativeCallResult`. Add new native classes in `dispatch.cpp` and implement the handler following the pattern in `handlers.hpp`. Unknown constructors/void calls are no-ops; unknown methods with return values return placeholder values.

## Resource Pack Format

`.pack` files: `"J2PK"` magic + version + entry table + concatenated file bytes. Built by `scripts/build_resource_pack.py` in the host repo (`esp-j2me-poc`), loaded by `jvm_resource_pack.cpp`. `J2MECompat` checks `<resourceRoot>.pack` before falling back to raw files.

## Conventions

- No app-specific code anywhere — all J2ME feature additions must be generic
- `#ifdef ESP32_BUILD` / `#ifdef PC_BUILD` for platform branches
- `J2MECompat.cpp` must not use `std::filesystem`; use `esp_gallery::Fs` (embedded) or plain `fopen("rb")` (host)
- Diagnostic logging uses `[tag]` prefixes (e.g. `[new]`, `[putfield-hot]`) and is wrapped in `#ifdef ESP32_BUILD`
- Patches in repo root (`*.patch`) are unapplied `git diff` snapshots — apply with `git apply <file>.patch`

## Optimisation Discipline

Before claiming a perf win for a proposed change, verify the **new mechanism is structurally cheaper than the baseline**, not just different.

- `unordered_map` lookups all cost roughly the same (hash + bucket walk + indirection) regardless of "how hot" the cache claims to be. Adding a second `unordered_map` in front of an existing one is a net loss — you pay two lookups on miss, gain nothing on hit. Real inline caches use array index, bytecode rewriting, or per-call-site immediate state — they avoid hashing entirely.
- Bytecode-profile bucket totals do **not** capture per-invoke setup cost. Frame allocation, `executeMethod` entry/return, `callStack.push_back` etc. live in the *gap* between bucket timers. When evaluating an invoke-related optimisation, compare `tasks=` or `task_invoke=` totals across runs — never `invokeDisp` alone.
- Profile overhead scales with bytecode-profiling state. Don't compare a `JVM_ENABLE_BYTECODE_PROFILING=1` slow frame against a `=0` baseline; profile adds ~50+ ms per slow frame at typical step counts.
- After-the-fact lesson when an optimisation regresses: write it into `docs/<area>-options.md` as a "Tried, regressed" entry with the *why* (cache shape, measurement mistake, missing structural difference). General principles get recorded here.

## Docs

- `docs/jvm-exception-handling.plan.md` — exception dispatch implementation plan (exception tables are already parsed)
