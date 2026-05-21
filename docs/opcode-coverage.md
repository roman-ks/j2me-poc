# JVM Opcode Coverage

Cross-reference of every CLDC 1.1 bytecode against what `tools/jvm-poc/interpreter.cpp` does.

## Status legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Handled correctly |
| ⚠️ | In switch but approximated (notes explain) |
| ❌ | Not in switch — `default: pc += instructionLength(op)`. No stack change. |
| ❌🔴 | Not in switch AND the skip itself is wrong (bad PC advance or wrong stack balance with downstream consequences) |
| N/A | Not part of CLDC 1.1 |

"Not in switch" = silent no-op that advances PC without touching the operand stack. Whether this is benign depends on the opcode's stack delta (see Impact column).

---

## Table

### Constants (0x00–0x14)

| Hex | Mnemonic | Status | Impact of current behavior |
|-----|----------|--------|---------------------------|
| 0x00 | nop | ❌ | Benign — no stack effect needed |
| 0x01 | aconst_null | ✅ | — |
| 0x02 | iconst_m1 | ✅ | — |
| 0x03 | iconst_0 | ✅ | — |
| 0x04 | iconst_1 | ✅ | — |
| 0x05 | iconst_2 | ✅ | — |
| 0x06 | iconst_3 | ✅ | — |
| 0x07 | iconst_4 | ✅ | — |
| 0x08 | iconst_5 | ✅ | — |
| 0x09 | lconst_0 | ✅ | — |
| 0x0a | lconst_1 | ✅ | — |
| 0x0b | fconst_0 | ✅ | — |
| 0x0c | fconst_1 | ✅ | — |
| 0x0d | fconst_2 | ✅ | — |
| 0x0e | dconst_0 | ✅ | — |
| 0x0f | dconst_1 | ✅ | — |
| 0x10 | bipush | ✅ | — |
| 0x11 | sipush | ✅ | — |
| 0x12 | ldc | ✅ | — |
| 0x13 | ldc_w | ✅ | — |
| 0x14 | ldc2_w | ✅ | — |

### Loads (0x15–0x35)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0x15 | iload | ✅ | — |
| 0x16 | lload | ✅ | — |
| 0x17 | fload | ✅ | — |
| 0x18 | dload | ✅ | — |
| 0x19 | aload | ✅ | — |
| 0x1a–0x1d | iload_0–3 | ✅ | — |
| 0x1e–0x21 | lload_0–3 | ✅ | — |
| 0x22–0x25 | fload_0–3 | ✅ | — |
| 0x26–0x29 | dload_0–3 | ✅ | — |
| 0x2a–0x2d | aload_0–3 | ✅ | — |
| 0x2e | iaload | ✅ | — |
| 0x2f | laload | ✅ | — |
| 0x30 | faload | ✅ | — |
| 0x31 | daload | ✅ | — |
| 0x32 | aaload | ✅ | — |
| 0x33 | baload | ✅ | — |
| 0x34 | caload | ✅ | — |
| 0x35 | saload | ✅ | — |

### Stores (0x36–0x56)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0x36 | istore | ✅ | — |
| 0x37 | lstore | ✅ | — |
| 0x38 | fstore | ✅ | — |
| 0x39 | dstore | ✅ | — |
| 0x3a | astore | ✅ | — |
| 0x3b–0x3e | istore_0–3 | ✅ | — |
| 0x3f–0x42 | lstore_0–3 | ✅ | — |
| 0x43–0x46 | fstore_0–3 | ✅ | — |
| 0x47–0x4a | dstore_0–3 | ✅ | — |
| 0x4b–0x4e | astore_0–3 | ✅ | — |
| 0x4f | iastore | ✅ | — |
| 0x50 | lastore | ✅ | — |
| 0x51 | fastore | ✅ | — |
| 0x52 | dastore | ✅ | — |
| 0x53 | aastore | ✅ | — |
| 0x54 | bastore | ✅ | — |
| 0x55 | castore | ✅ | — |
| 0x56 | sastore | ✅ | — |

### Stack (0x57–0x5f)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0x57 | pop | ✅ | — |
| 0x58 | pop2 | ✅ | — |
| 0x59 | dup | ✅ | — |
| 0x5a | dup_x1 | ✅ | — |
| 0x5b | dup_x2 | ✅ | — |
| 0x5c | dup2 | ✅ | — |
| 0x5d | dup2_x1 | ✅ | — |
| 0x5e | dup2_x2 | ✅ | — |
| 0x5f | swap | ✅ | — |

### Arithmetic (0x60–0x84)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0x60 | iadd | ✅ | — |
| 0x61 | ladd | ✅ | — |
| 0x62 | fadd | ✅ | — |
| 0x63 | dadd | ✅ | — |
| 0x64 | isub | ✅ | — |
| 0x65 | lsub | ✅ | — |
| 0x66 | fsub | ✅ | — |
| 0x67 | dsub | ✅ | — |
| 0x68 | imul | ✅ | — |
| 0x69 | lmul | ✅ | — |
| 0x6a | fmul | ✅ | — |
| 0x6b | dmul | ✅ | — |
| 0x6c | idiv | ✅ | — |
| 0x6d | ldiv | ✅ | — |
| 0x6e | fdiv | ✅ | — |
| 0x6f | ddiv | ✅ | — |
| 0x70 | irem | ✅ | — |
| 0x71 | lrem | ✅ | — |
| 0x72 | frem | ✅ | — |
| 0x73 | drem | ✅ | — |
| 0x74 | ineg | ✅ | — |
| 0x75 | lneg | ✅ | — |
| 0x76 | fneg | ✅ | — |
| 0x77 | dneg | ✅ | — |
| 0x78 | ishl | ✅ | — |
| 0x79 | lshl | ✅ | — |
| 0x7a | ishr | ✅ | — |
| 0x7b | lshr | ✅ | — |
| 0x7c | iushr | ✅ | — |
| 0x7d | lushr | ✅ | — |
| 0x7e | iand | ✅ | — |
| 0x7f | land | ✅ | — |
| 0x80 | ior | ✅ | — |
| 0x81 | lor | ✅ | — |
| 0x82 | ixor | ✅ | — |
| 0x83 | lxor | ✅ | — |
| 0x84 | iinc | ✅ | — |

### Conversions (0x85–0x93)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0x85 | i2l | ✅ | — |
| 0x86 | i2f | ✅ | — |
| 0x87 | i2d | ✅ | — |
| 0x88 | l2i | ✅ | — |
| 0x89 | l2f | ✅ | — |
| 0x8a | l2d | ✅ | — |
| 0x8b | f2i | ✅ | — |
| 0x8c | f2l | ✅ | — |
| 0x8d | f2d | ✅ | — |
| 0x8e | d2i | ✅ | — |
| 0x8f | d2l | ✅ | — |
| 0x90 | d2f | ✅ | — |
| 0x91 | i2b | ✅ | — |
| 0x92 | i2c | ✅ | — |
| 0x93 | i2s | ✅ | — |

### Comparisons (0x94–0xa6)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0x94 | lcmp | ✅ | — |
| 0x95 | fcmpl | ✅ | — |
| 0x96 | fcmpg | ✅ | — |
| 0x97 | dcmpl | ✅ | — |
| 0x98 | dcmpg | ✅ | — |
| 0x99–0x9e | ifeq/ne/lt/ge/gt/le | ✅ | — |
| 0x9f–0xa4 | if_icmpeq/ne/lt/ge/gt/le | ✅ | — |
| 0xa5 | if_acmpeq | ✅ | — |
| 0xa6 | if_acmpne | ✅ | — |

### Control (0xa7–0xb1)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0xa7 | goto | ✅ | — |
| 0xa8 | jsr | N/A | Forbidden by CLDC 1.1 verifier |
| 0xa9 | ret | N/A | Forbidden by CLDC 1.1 verifier |
| 0xaa | tableswitch | ✅ | — |
| 0xab | lookupswitch | ✅ | — |
| 0xac | ireturn | ✅ | — |
| 0xad | lreturn | ✅ | — |
| 0xae | freturn | ✅ | — |
| 0xaf | dreturn | ✅ | — |
| 0xb0 | areturn | ✅ | — |
| 0xb1 | return (void) | ✅ | — |

### References (0xb2–0xbf)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0xb2 | getstatic | ✅ | — |
| 0xb3 | putstatic | ✅ | — |
| 0xb4 | getfield | ✅ | — |
| 0xb5 | putfield | ✅ | — |
| 0xb6 | invokevirtual | ✅ | — |
| 0xb7 | invokespecial | ✅ | — |
| 0xb8 | invokestatic | ✅ | — |
| 0xb9 | invokeinterface | ✅ | — |
| 0xba | invokedynamic | N/A | Java 7+, not in CLDC 1.1 |
| 0xbb | new | ✅ | — |
| 0xbc | newarray | ✅ | Types 4,5,6,8,9,10 → CompactArrayHeap (int32_t/element). Types 7,11 → object array with Value::ofLong(0) init |
| 0xbd | anewarray | ✅ | — |
| 0xbe | arraylength | ✅ | — |
| 0xbf | athrow | ✅ | — |

### Extended (0xc0–0xc9)

| Hex | Mnemonic | Status | Impact |
|-----|----------|--------|--------|
| 0xc0 | checkcast | ❌ | Benign: objectref stays on stack after 3-byte skip. No type check performed. |
| 0xc1 | instanceof | ⚠️ | Pops ref, pushes 0. Correct stack balance; always-false is conservative — no runtime type info |
| 0xc2 | monitorenter | ✅ | Pops ref, no-op (single-threaded JVM) |
| 0xc3 | monitorexit | ✅ | Same |
| 0xc4 | wide | ❌🔴 | Prefix that changes next opcode to 16-bit index. Default +1 advance re-enters the switch at the nested opcode byte — it will mis-execute rather than skip. |
| 0xc5 | multianewarray | ✅ | — |
| 0xc6 | ifnull | ✅ | — |
| 0xc7 | ifnonnull | ✅ | — |
| 0xc8 | goto_w | ❌🔴 | 5-byte instruction. Default +1 PC advance mis-aligns; subsequent opcodes execute garbage bytes. |
| 0xc9 | jsr_w | N/A | Forbidden by CLDC 1.1 verifier |

---

## Summary counts

| Category | Count |
|----------|-------|
| ✅ Handled | 132 |
| ⚠️ Partial | 1 (instanceof always-false) |
| ❌ / ❌🔴 Missing | 2 (wide 0xc4; goto_w 0xc8) |
| N/A | 5 |

---

## Remaining gaps

### `wide` (0xc4) — wrong PC advance on hit

`wide` is a prefix opcode that widens the immediately-following load/store/iinc to use a 2-byte local variable index instead of 1-byte. The `default:` branch advances PC by 1 (`instructionLength` returns 1 for 0xc4), which re-enters the switch at the inner opcode byte — mis-executing it as a standalone instruction.

Correct handler: read the nested opcode at `code[pc+1]`, dispatch the wide form of iload/lload/fload/dload/aload/istore/lstore/fstore/dstore/astore/iinc using `codeU2(code, pc+2)` as the variable index, then advance PC by 3 (or 6 for `wide iinc`, which also has a 2-byte constant). Rare in practice — CLDC 1.1 class files rarely need >255 locals — but causes silent mis-execution when hit.

### `goto_w` (0xc8) — wrong PC advance on hit

5-byte instruction with a 4-byte signed branch offset. The `default:` branch advances PC by 1, mis-aligning everything that follows. Only emitted when a jump offset exceeds ±32767 bytes; extremely rare in CLDC 1.1 methods. Fix: read `codeS4(code, pc+1)`, compute `branchTarget(pc, offset)`, assign to `pc`.

### `instanceof` (0xc1) — always returns false

Stack balance is correct (pops ref, pushes int). The result is hardcoded to 0. This is safe when `instanceof` is used as an optional optimisation guard, but will break code that branches on it to determine actual type — e.g. polymorphic dispatch patterns or null-safe casts. Fixing requires a type tag on `HeapObject` and a lookup against the class hierarchy.

### Benign / not worth fixing

- **`nop` (0x00)** — 1-byte, no stack effect; default skip is correct.
- **`checkcast` (0xc0)** — 3-byte, no stack delta (objectref stays); default skip leaves stack intact and silently skips type validation, which is fine for a non-verifying runtime.
