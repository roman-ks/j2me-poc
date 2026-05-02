# J2ME POC

Tiny MIDP 2.0 proof of concept.

## Build

```bash
mvn package
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
