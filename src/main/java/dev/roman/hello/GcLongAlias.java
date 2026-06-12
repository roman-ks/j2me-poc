package dev.roman.hello;

// Regression test for the compact field layout (vector<int32_t> fields).
//
// A long field is stored as two untagged 32-bit words. If GC traced object
// fields by scanning every word and asking "does this look like a handle?"
// (as it did when fields were tagged Values), a long whose low word equals a
// live object handle would be mis-traced and wrongly kept alive. The compact
// layout traces only descriptor-derived reference slots (fieldRefSlots), so a
// long's words are never examined.
//
// Layout / handle order (first user `new` == obj#1):
//   obj#1  victim   — garbage (allocated in a sub-method, dropped on return)
//   obj#2  holder   — reachable via the static field
//   obj#3  live     — reachable ONLY via holder.ref
// holder.aliased's low 32 bits are set to obj#1's handle (0x01000001). Correct
// GC frees obj#1; a naive word-scan would mis-trace it and keep it alive.
public class GcLongAlias {
    static LongAliasHolder holder;

    static int makeVictim() {
        Objects victim = new Objects(99); // obj#1; local dies on return → garbage
        return victim.add(1);
    }

    static void attachLive() {
        holder.ref = new Objects(5); // obj#3; reachable only through holder.ref
    }

    public static void main(String[] args) {
        NativeRuntime.printInt(makeVictim());          // 100; obj#1 now garbage
        holder = new LongAliasHolder();                // obj#2 (reachable via static)
        holder.aliased = 0x01000001L;                  // low word aliases obj#1 handle
        NativeRuntime.printInt((int) holder.aliased);  // 16777217; long round-trip
        attachLive();                                  // obj#3 via holder.ref only
        System.gc();
        NativeRuntime.printInt(holder.ref.add(1));     // 6; obj#3 survived ref-trace
    }
}
