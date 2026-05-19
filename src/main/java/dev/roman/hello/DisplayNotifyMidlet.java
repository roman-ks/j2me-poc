package dev.roman.hello;

import javax.microedition.lcdui.Display;
import javax.microedition.midlet.MIDlet;

public final class DisplayNotifyMidlet extends MIDlet {
    private final DisplayNotifyCanvas first = new DisplayNotifyCanvas("first");
    private final DisplayNotifyCanvas second = new DisplayNotifyCanvas("second");

    protected void startApp() {
        Display display = Display.getDisplay(this);
        display.setCurrent(first);
        display.setCurrent(second);
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
