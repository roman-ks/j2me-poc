package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;

public abstract class GameCanvas extends Canvas {
    protected GameCanvas(boolean suppressKeyEvents) {
        super();
    }

    public native Graphics getGraphics();
    public native void flushGraphics();
    public native void flushGraphics(int x, int y, int width, int height);

    public void paint(Graphics g) {
    }
}
