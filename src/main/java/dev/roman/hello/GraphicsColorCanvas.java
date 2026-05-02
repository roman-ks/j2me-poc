package dev.roman.hello;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;

final class GraphicsColorCanvas extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(0x0000FF);
        g.fillRect(0, 0, getWidth(), getHeight());
        g.setColor(0xFF0000);
        g.fillRect(0, 0, 10, 10);
    }
}