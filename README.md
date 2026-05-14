# Tiny J2ME runtime

Tiny MIDP 2.0/CLDC 1.1 implementation targeting embedded ESP32-S3 and Linux.

## Build

```bash
mvn package
```

To build only the boot classes under `src/jvm-boot/java`:

```bash
./tools/build-jvm-boot-classes.sh
```

## Inspect Classes

```bash
make -C tools/classdump
tools/classdump/classdump \
  target/classes/dev/roman/j2mepoc/HelloMidletMini.class \
  'target/classes/dev/roman/j2mepoc/HelloMidletMini$MyCanvas.class'
```

`classdump` does not execute bytecode. It loads `.class` files and reports the
object field slots, static field slots, method argument slots, `max_locals`,
`max_stack`, bytecode size, and debug local-variable names when present.

## JVM POC

```bash
make -C tools/jvm-poc
tools/jvm-poc/jvm-poc \
  target/classes/dev/roman/hello/Nums.class \
  target/classes/dev/roman/hello/BranchNums.class \
  target/classes/dev/roman/hello/BooleanOps.class \
  target/classes/dev/roman/hello/LoopFor.class \
  target/classes/dev/roman/hello/LoopWhile.class \
  target/classes/dev/roman/hello/LoopDoWhile.class \
  target/classes/dev/roman/hello/StaticMethods.class \
  target/classes/dev/roman/hello/StaticFields.class \
  target/classes/dev/roman/hello/Objects.class \
  target/classes/dev/roman/hello/IntArrays.class \
  target/classes/dev/roman/hello/Strings.class \
  target/classes/dev/roman/hello/StringLength.class \
  target/classes/dev/roman/hello/NativeRuntime.class
```

`jvm-poc` is the extensible toy-runtime path. It currently shares the same class
inspection basics and also has a tiny straight-line integer pass for local
writes, arithmetic (`+`, `-`, `*`, `/`), boolean locals as `0`/`1`, branches,
simple `for`/`while`/`do while` loops, and static method calls with int args and
int returns. It also supports `static int` fields through `getstatic` and
`putstatic`, plus basic object allocation, int instance fields, constructors,
instance methods for loaded classes, and int arrays (`newarray`, `iaload`,
`iastore`, `arraylength`). String literals are represented by native `str#`
handles, can be passed/stored like other refs, support `String.length()`, and
can be printed through `NativeRuntime.printString(String)`. It recognizes the
temporary native hooks `NativeRuntime.printInt(int)`, `printString(String)`, and
`gc()`.

Host applications should embed the JVM through `JvmMidletApp` instead of letting
the JVM own platform services. The host implements `JvmHost` for screen size,
time, and framebuffer presentation, then loads classes and starts a MIDlet.
The current facade has `loadClasses`, `setClasses`, `start`, and a placeholder
`render` method; renderer/input wiring comes next.

A minimal SDL host executable proves the embedding direction:

```bash
make -C tools/jvm-poc jvm-poc-sdl-demo
tools/jvm-poc/jvm-poc-sdl-demo dev/roman/j2mepoc/HelloMidletMini \
  target/classes/dev/roman/j2mepoc/HelloMidletMini.class \
  'target/classes/dev/roman/j2mepoc/HelloMidletMini$MyCanvas.class'
```

For now it opens a 240x320 window, starts the MIDlet through `JvmMidletApp`, and
presents a placeholder framebuffer. Actual Display/Canvas/Graphics rendering is
the next layer.

By default `jvm-poc` prints only the runtime phase and runs
`public static main(String[] args)` when present. Add `--metadata` to include
the class structure dump before runtime output, or `--stdout-only` to print just
native stdout values for tests. Loops are guarded by a simple bytecode step
limit. Unknown method calls are logged; unknown constructors and void calls are
treated as no-ops, while unknown methods with return values produce placeholder
values.

MIDlet classes can be started without a `main` by constructing the class and
calling `startApp()`:

```bash
tools/jvm-poc/jvm-poc --midlet dev/roman/j2mepoc/HelloMidletMini \
  target/classes/dev/roman/j2mepoc/HelloMidletMini.class \
  'target/classes/dev/roman/j2mepoc/HelloMidletMini$MyCanvas.class'
```

Run the current toy-runtime assertions with:

```bash
make -C tools/jvm-poc test
```

## Run

FreeJ2ME is the preferred emulator for this POC:

```bash
ant -f tools/freej2me/build.xml
mvn package
./tools/run-freej2me.sh
```

To run a different JAR:

```bash
./tools/run-freej2me.sh /absolute/path/to/game.jar
```

Optional screen arguments are `width height scale`:

```bash
./tools/run-freej2me.sh target/j2me-poc-0.1.0.jar 240 320 3
```

The packaged MIDlet is written to `target/j2me-poc-0.1.0.jar`.

This project uses `.mvn/maven.config` so Maven downloads dependencies into the
repo-local `.m2/repository` directory instead of `~/.m2/repository`.

Real phones and stricter emulators may also require preverification, which is
not part of this first minimal setup.
