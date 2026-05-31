# Repaint and Frame Presentation

How the JVM decides when to call `paint()` and when to push a framebuffer to the host.

## Architecture

```
JvmMidletApp::render()
  └─ renderMidletSession()          ← one "tick"
       ├─ run tasks (game threads)  ← may hit step limit, yield early
       └─ if repaintRequested:
            call paint()            ← may be empty (GameCanvas)
            set trace.framePresented
  └─ if trace.framePresented:
       host_.present()              ← push framebuffer to SDL / display
```

Key fields in `Runtime`:
- `repaintRequested` — set by `repaint()` or `flushGraphics()`, cleared after `paint()` is called in `renderSession`
- `gameCanvasFlushCommitted` — set ONLY by `flushGraphics()`, not by `repaint()`; cleared after `paint()`
- `trace.framePresented` — controls whether `host_.present()` is called this tick

## Regular Canvas

Game overrides `paint(Graphics g)`. The system calls it when `repaintRequested`.

```
game thread:   repaint() ──────────────────────────────────┐
                                                            │ sets repaintRequested
renderSession: tasks run → repaintRequested=true → paint() ─┘ → framePresented=true → present
```

`framePresented = !isGameCanvas || gameCanvasFlushCommitted`
For regular Canvas: `!isGameCanvas = true`, so paint() always triggers present.

## GameCanvas

Game uses `getGraphics()` to draw into the **main framebuffer directly** (there is no separate off-screen buffer — the main framebuffer is reused). When done, it calls `flushGraphics()` to signal the frame is complete.

```
game thread:   draw to main fb ─→ aq() draws corners ─→ flushGraphics() ─→ sleep()
                                                          │
                                              sets gameCanvasFlushCommitted=true
                                              sets repaintRequested=true

renderSession: tasks done → repaintRequested=true → paint() (empty)
               gameCanvasFlushCommitted=true → framePresented=true → present  ✓
```

### Why `repaint()` must NOT trigger present for GameCanvas

Timer threads (e.g. class `f`) call `repaint()` on the game canvas at arbitrary times — including while the render thread is mid-frame. At that point the framebuffer may contain:

- the off-screen world image (just blitted via `drawImage`) but  
- **not yet** the HUD / corner buttons drawn by `aq()`

If `repaint()` triggered present, the user sees a partial frame with no HUD, then the complete frame — visible flicker.

Fix: `repaint()` sets only `repaintRequested`. `flushGraphics()` sets **both** `repaintRequested` and `gameCanvasFlushCommitted`. In `renderSession`:

```cpp
const bool isGameCanvas = isClassOrSubclassOf(classes, displayableClassName,
    "javax/microedition/lcdui/game/GameCanvas");
rt.trace.framePresented = !isGameCanvas || rt.gameCanvasFlushCommitted;
rt.gameCanvasFlushCommitted = false;
```

## Why present is NOT unconditional

Before the fix, `JvmMidletApp::render()` called `host_.present()` after every `renderSession`. This caused a second class of flicker:

The step limit (`kMaxSteps = 100000`) can interrupt the game thread **between**:
1. `drawImage(offscreen, ...)` — copies world image to main fb, erasing old corners
2. `aq()` — draws corner buttons on top

If `renderSession` presented after step 1 but before step 2, the user saw a frame without corners for one tick.

Fix: `host_.present()` is conditional on `trace.framePresented`:

```cpp
// jvm_midlet_app.cpp
lastTrace_ = renderMidletSession(*session_, fb, width, height);
if (lastTrace_.framePresented) {
    host_.present(fb, width, height);
}
```

When the step limit fires mid-render, `repaintRequested` is still false (flushGraphics not reached yet), so `paint()` is skipped, `framePresented = false`, and the partial frame is never shown. The SDL loop delays only 1ms (`stepLimitHit ? 1 : 16`), so the render completes on the next tick.

## Summary of flags

| Event | `repaintRequested` | `gameCanvasFlushCommitted` | `framePresented` |
|---|---|---|---|
| `repaint()` on regular Canvas | ✓ | — | ✓ (after paint) |
| `repaint()` on GameCanvas | ✓ | ✗ | ✗ |
| `flushGraphics()` on GameCanvas | ✓ | ✓ | ✓ (after paint) |
| step-limit mid-render, no flush yet | ✗ | ✗ | ✗ |
