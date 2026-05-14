# J2ME POC — Agent Instructions

Production-grade, performance-first embedded JVM runtime in C++, targeting ESP32 and x86 Linux. It implements a subset of CLDC 1.1 / MIDP 2.0 — "tiny" refers to an incomplete (but growing) standard-library coverage, **not** to code quality or runtime performance. See [README.md](README.md) for full build/run commands.

> **Important for agents:** Do not treat the "POC" name as a reason to favour quick-and-dirty implementations over correct, efficient ones. Every change must be as if shipping to a memory- and CPU-constrained ESP32 device in production.

## Build Commands

| Target | Command |
|--------|---------|
| Java MIDlet JAR | `mvn package` |
| Boot classes only | `./tools/build-jvm-boot-classes.sh` |
| JVM C++ runtime | `make -C tools/jvm-poc` |
| JVM tests | `make -C tools/jvm-poc test` (also runs `mvn package`) |
| SDL demo | `make -C tools/jvm-poc jvm-poc-sdl-demo` |
| classdump tool | `make -C tools/classdump` |
| FreeJ2ME emulator | `ant -f tools/freej2me/build.xml && mvn package && ./tools/run-freej2me.sh` |

Maven uses a repo-local `.m2/repository` via `.mvn/maven.config` — never `~/.m2`.

## Project Layout

```
src/jvm-boot/java/      Boot CLDC/MIDP stub classes (compiled by build-jvm-boot-classes.sh)
src/main/java/          MIDlet source (dev/roman/…)
tools/jvm-poc/          C++ JVM runtime (interpreter, heap, native methods)
tools/freej2me/         FreeJ2ME emulator (ant build, used for full MIDP playback)
tools/classdump/        Offline .class file inspector
config/                 Per-game runtime config files
docs/                   Architecture plans (e.g. jvm-exception-handling.plan.md)
```

## Architecture

### C++ Runtime (`tools/jvm-poc/`)

| File | Role |
|------|------|
| `class_file.cpp` | Parses Java 1.3 .class files; resolves constant pool, exception handler tables |
| `value.hpp` | Flat 12-byte tagged union. References are 24-bit H1 handles, not pointers |
| `frame.cpp` | Per-call locals[] + operand stack[], both using `SramAllocator` |
| `interpreter.cpp` | Single-pass bytecode dispatch; step limit `kMaxSteps=100000` per frame |
| `method_resolution.cpp` | `findMethodInHierarchy()` searches superclass chain |
| `jvm_midlet_app.cpp` | Public host API: `loadClasses`, `start(className)`, `render()` |
| `extracted_midlet.cpp` | Reads MANIFEST.MF MIDlet-1 entry; appends boot classes |
| `native_methods/dispatch.cpp` | Routes invokestatic/invokevirtual to handler by class name |
| `j2me_port/J2MECompat.cpp` | Resource loading, RecordStore (RMS), J2ME shims |
| `j2me_port/Canvas.cpp` | 2D framebuffer: RGB565 pixels, RLE alpha, pluggable image decoder |
| `core_shim/` | ESP32 ↔ x86 portability: log macros, `esp_gallery::Fs`, `SramAllocator` |

### H1 Handle Encoding
References are stored as `Tag::kInt` with the high byte encoding type:
- `0x01xxxxxx` — object (obj#N)
- `0x02xxxxxx` — array (arr#N)
- `0x03xxxxxx` — image (image#N)
- `0x04xxxxxx` — graphics context (gfx:image#N)

### Native Method Pattern
Each native handler in `native_methods/` receives `NativeCallContext& ctx` and returns `NativeCallResult`. Add new native classes in `dispatch.cpp` and implement the handler following the pattern in `handlers.hpp`.

### Boot Classes (`src/jvm-boot/java/`)
Hand-written Java stubs for `java/lang/`, `java/io/`, `java/util/`, and `javax/microedition/`. Key constraint: **avoid bitwise ops** in boot Java unless the opcode is confirmed supported by the interpreter (e.g. use `if (b < 0) b += 256` instead of `b & 255`).

`java/lang/String` constructors all funnel through `private native init([CII)V`. GC traces still identify string objects by `className == "java/lang/String"`.

## Key Conventions

- **Performance is non-negotiable.** This runtime runs on ESP32. Never sacrifice performance with "good enough for a POC" reasoning. Prefer stack allocation, avoid heap churn, and keep hot paths branch-free where possible.
- **Embedding:** Host apps use `JvmMidletApp` (not a standalone JVM). Host implements `JvmHost` for screen size, time, and framebuffer presentation.
- **Portability:** `J2MECompat.cpp` must not depend on `std::filesystem`; use `esp_gallery::Fs` for embedded or plain `fopen("rb")` for host fallback.
- **Unknown calls:** Unknown method calls are logged; unknown constructors/void calls are no-ops; unknown methods with return values return placeholder values.
- **Exception table:** `MethodInfo` already carries parsed exception handler entries (`startPc`, `endPc`, `handlerPc`, `catchType`). Catch-all handlers have `catchType=0`. See [docs/jvm-exception-handling.plan.md](docs/jvm-exception-handling.plan.md) for the implementation plan.
- **Tests:** `tools/jvm-poc/tests.cpp` + `make -C tools/jvm-poc test`. Tests require the Java classes to be compiled first (`mvn package` is run automatically by the test target).

## Docs to Consult

- [README.md](README.md) — full command reference and feature status
- [docs/jvm-exception-handling.plan.md](docs/jvm-exception-handling.plan.md) — exception dispatch implementation plan
