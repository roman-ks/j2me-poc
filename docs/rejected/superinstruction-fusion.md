# Superinstruction fusion (peek-ahead opcode pairs)

Fusing a common adjacent bytecode pair `(A, B)` into a single dispatch by
peek-ahead: in `A`'s handler, check `code[pc+len(A)] == B` and, on a hit, do
both operations in one loop iteration (skipping `B`'s separate dispatch).

This is the classic interpreter superinstruction technique (Ertl & Gregg, *The
Structure and Performance of Efficient Interpreters*, 2003). It pays off only
under specific conditions on **this** runtime — most candidate pairs do not meet
them. Sibling to [field-slot-inline-cache.md](field-slot-inline-cache.md) and
[heap-lookup-options.md](heap-lookup-options.md): all three found that a
*measured-hot* construct did not translate into a bankable win.

Entries follow the template at the bottom. Add new ones above it.

---

## `iconst_2; idiv` → divide-by-2 (and the peek-ahead rule it taught us)

**Date:** 2026-06-15
**Status:** Reverted — −0.2 fps (min/avg/max, profiling off). Reverted in the
inverse of commit `8fd2017`.

### Hypothesis
`iconst_2; idiv` (≈2300×/heavy frame on game4) divides by 2. Integer division
is expensive on Xtensa, and `x / 2` is a shift — so fusing should remove a real
*division* (not just a dispatch), making it a better-than-average fusion.

### Change
- Peek-ahead in the `iconst_2` (`0x05`) handler: if next byte is `idiv`
  (`0x6c`), pop the dividend, compute `(v + (uint(v) >> 31)) >> 1` (sign-bit
  correction — Java `idiv` truncates toward zero, so a plain `>>1` is wrong for
  negatives), push, skip the `idiv`. Fall back to the normal push otherwise.
- Correctness was fine (regression test covered `-3/2 == -1`, `-1/2 == 0`).

### Measurement
game4, profiling off: −0.2 fps on min, avg, and max, consistently. The
i-cache-alignment noise floor on this interpreter is ~0.2–0.4 fps from a *single
unrelated line* (confirmed earlier with the `0x12345678`/`0xff` layout probe), so
−0.2 fps is at best a wash and at worst a real regression — never a win.

### Root cause
The peek branch is paid on **every** `iconst_2`, but `iconst_2` is followed by
`idiv` only ~half the time (push-2 is used for many things). A branch that goes
"yes" ~40–50% of the time is the **worst case for the branch predictor** — it
mispredicts ~half the time (~15–20 cyc each), on every `iconst_2`. That, plus the
i-cache layout shift from growing the dispatch function, swamps the benefit of
removing a division on the ~2300 hits. Net negative.

The earlier break-even estimate ("~3% hit rate, the peek is only 1–2 cycles") was
wrong: it counted the *predicted*-branch cost and ignored **misprediction** and
**code-layout** cost. Those dominate at middling hit rates.

### Lesson
A peek-ahead superinstruction pays off only when the first opcode is followed by
the second with **extreme probability (>~85%)**, so the peek branch predicts
near-perfectly and the saved dispatch lands on a large absolute count. That is
exactly why `aload_0; getfield` (`this.field`) **is** kept — `aload_0` is ~24% of
ops and ~90% of them are followed by `getfield`, so its peek is almost always
taken. Middling hit rates (~40–60%) lose to misprediction + layout, regardless of
how hot the pair looks in absolute counts, and regardless of whether the fusion
removes real work. Do **not** rank fusion candidates by pair count alone; rank by
`P(B | A)` (= `count(A→B) / count(A)`, both already available from `[pairs]` +
`[ops]`), and only fuse when it's near-1.

### Related ideas not tried (same root cause → don't peek-ahead them)
- **`this.field; this.field`** (`0xfb>0xfb`, ~9200×/frame, the single largest
  pair). Tempting for its count, but `P(this.field | this.field) ≈ 42%` — the
  same poorly-predicted regime that sank divide-by-2, in an even hotter handler.
  Skipped.
- **`iadd; putfield`**, **`iconst_0; baload`**, **`this.field; aaload`** — all
  middling hit rates; same wall.
- **Bytecode rewriting** is the *only* way to harvest these without the
  misprediction: at class-load, rewrite the byte sequence to a synthetic opcode
  with its own `case` (no runtime peek, so hit rate is irrelevant). It's how
  JamVM/CVM did it. Not done — it needs a writable bytecode copy, a load-time
  pass, and branch-target safety (a jump must never land mid-pair). Only worth it
  as a deliberate investment, and even then the ceiling is small here: dispatch
  is only ~10% of our per-op cost (the other 90% is PSRAM-resident reads, 12-byte
  `Value` copies, per-op scaffolding), so the textbook 20–30% superinstruction
  speedup — which assumes a dispatch-bound interpreter — does not apply.

### Note: what *is* kept
The `aload_0; getfield` → `this.field` superinstruction (commit `f92b09e`) is the
one fusion that cleared the bar (high frequency × ~90% hit rate) and is retained.
It is the exception that proves the rule, not a precedent for more peek-ahead
pairs.

---

## Template — copy this for new entries

```
## <Short, specific name of the attempt>

**Date:** YYYY-MM-DD
**Status:** Reverted | Kept (neutral) | Kept (win: …) | Partially kept (…)

### Hypothesis
What did we believe was slow, and what mechanism would make it faster?

### Change
3–6 bullets. What code actually changed, terse.

### Measurement
Numbers. Before / after. State the noise floor and the profiling state.

### Root cause
Why the result came out the way it did. Mechanism, not vibes.

### Lesson
The generalizable principle. One short paragraph. Future-you reads only this
section when deciding whether to try a similar idea.

### Related ideas not tried
Variants sharing the same root cause; list them so they don't get re-proposed.
```

#### raw perf logs
```
# superinstruction + heap flat array(step2) 31540e79fd6dd61d638fcc7788e72be9cf0d0eb4
[src/core/app/jvm_controller.cpp:162]    [INFO ]   gc e.ea()V pc=138 took=65ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.6 min=0.3 max=9.9 | frame avg=380099us min=100913us max=3147720us | window=22805ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.8 min=2.6 max=3.0 | frame avg=360025us min=333619us max=387993us | window=21601ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.8 min=2.6 max=3.0 | frame avg=362752us min=337513us max=388185us | window=21765ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.7 min=2.3 max=2.9 | frame avg=368735us min=341463us max=435638us | window=22124ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.7 min=2.4 max=2.9 | frame avg=372569us min=339668us max=418675us | window=22354ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.7 min=2.5 max=2.9 | frame avg=367696us min=342781us max=399672us | window=22061ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.7 min=2.5 max=2.9 | frame avg=366541us min=339715us max=397157us | window=21992ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.7 min=2.5 max=2.9 | frame avg=369420us min=343273us max=398602us | window=22165ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.7 min=2.5 max=2.9 | frame avg=371869us min=348366us max=406064us | window=22312ms

# div-by-2 superinstruction 8fd201728919330361587514b1116dbabd44a09f
[src/core/app/jvm_controller.cpp:162]    [INFO ]   gc e.ea()V pc=138 took=65ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.9 min=0.3 max=12.0 | frame avg=347159us min=83358us max=3477512us | window=20829ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.6 min=2.4 max=2.8 | frame avg=391575us min=362165us max=421845us | window=23494ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.6 min=2.4 max=2.8 | frame avg=387574us min=361046us max=418693us | window=23254ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.6 min=2.4 max=2.7 | frame avg=389331us min=364062us max=416004us | window=23359ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.5 min=2.3 max=2.8 | frame avg=392519us min=357876us max=427338us | window=23551ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.5 min=2.3 max=2.7 | frame avg=402072us min=376053us max=439167us | window=24124ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.5 min=2.3 max=2.7 | frame avg=400298us min=370500us max=434734us | window=24017ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.5 min=2.3 max=2.7 | frame avg=400843us min=375955us max=426494us | window=24050ms
[src/core/app/jvm_controller.cpp:249]    [INFO ] [fps] n=60 avg=2.4 min=2.2 max=2.6 | frame avg=409631us min=379990us max=444788us | window=24577ms
```