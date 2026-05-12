package dev.roman.hello;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;

final class DisplayNotifyCanvas extends Canvas {
    private final String name;

    DisplayNotifyCanvas(String name) {
        this.name = name;
    }

    public void showNotify() {
        NativeRuntime.printString(name + ":show");
    }

    public void hideNotify() {
        NativeRuntime.printString(name + ":hide");
    }

    protected void paint(Graphics g) {
        g.setColor(0xffffff);
        g.fillRect(0, 0, getWidth(), getHeight());
        g.setColor(0);
        g.fillRect(0, 0, 1, 1);
    }
}
