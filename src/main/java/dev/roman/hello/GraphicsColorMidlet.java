package dev.roman.hello;

import javax.microedition.lcdui.Display;
import javax.microedition.midlet.MIDlet;

public class GraphicsColorMidlet extends MIDlet {
    private final GraphicsColorCanvas canvas = new GraphicsColorCanvas();

    public void startApp() {
        Display.getDisplay(this).setCurrent(canvas);
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}