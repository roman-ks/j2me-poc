package dev.roman.hello;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.midlet.MIDlet;

public class InputMidlet extends MIDlet {
    private final Canvas canvas = new InputCanvas();

    public void startApp() {
        Display.getDisplay(this).setCurrent(canvas);
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}