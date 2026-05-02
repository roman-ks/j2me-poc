package javax.microedition.lcdui;

import javax.microedition.midlet.MIDlet;

public final class Display {
    private Display() {
    }

    public static native Display getDisplay(MIDlet midlet);

    public native void setCurrent(Displayable nextDisplayable);
}
