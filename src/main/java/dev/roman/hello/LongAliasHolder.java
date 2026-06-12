package dev.roman.hello;

// Heap object with a long field (two-word storage in the compact field layout)
// and a reference field. Used by GcLongAlias to prove that GC does not mis-trace
// a long bit pattern that happens to alias an object handle, while still tracing
// genuine reference fields. See docs/field-storage-compaction.plan.md.
public class LongAliasHolder {
    long aliased;
    Objects ref;
}
