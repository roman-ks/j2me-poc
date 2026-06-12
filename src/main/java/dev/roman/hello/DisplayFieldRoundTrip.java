package dev.roman.hello;

import javax.microedition.lcdui.Display;
import javax.microedition.midlet.MIDlet;

// Same as FontFieldRoundTrip but for Display, another kStr fake-handle
// ("display#1"). Correct: 1. Under the compact field layout: 0.
//
// EXPECTED TO FAIL until Font/Display are migrated to real H1 handles.
// See docs/field-storage-compaction.plan.md §7.
public class DisplayFieldRoundTrip {
    Display disp;

    public static void main(String[] args) {
        DisplayFieldRoundTrip o = new DisplayFieldRoundTrip();
        o.disp = Display.getDisplay((MIDlet) null);
        NativeRuntime.printInt(o.disp == Display.getDisplay((MIDlet) null) ? 1 : 0);
    }
}
