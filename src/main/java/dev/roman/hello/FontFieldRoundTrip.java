package dev.roman.hello;

import javax.microedition.lcdui.Font;

// Stores a Font in an instance field and checks it still equals a freshly
// fetched Font. Font is represented as a kStr fake-handle ("font:default"), and
// if_acmpeq compares kStr by content, so this is 1 when the field round-trips
// correctly. Under the compact field layout (vector<int32_t>), a kStr stored
// into a reference-typed field is corrupted to a garbage word, so this returns 0.
//
// EXPECTED TO FAIL until Font/Display are migrated to real H1 handles.
// See docs/field-storage-compaction.plan.md §7.
public class FontFieldRoundTrip {
    Font font;

    public static void main(String[] args) {
        FontFieldRoundTrip o = new FontFieldRoundTrip();
        o.font = Font.getDefaultFont();
        NativeRuntime.printInt(o.font == Font.getDefaultFont() ? 1 : 0);
    }
}
