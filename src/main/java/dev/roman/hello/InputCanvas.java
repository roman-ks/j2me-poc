package dev.roman.hello;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;

final class InputCanvas extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(255, 255, 255);
        g.fillRect(0, 0, getWidth(), getHeight());
        g.setColor(0, 0, 0);
        g.fillRect(1, 1, 1, 1);
    }

    protected void keyPressed(int keyCode) {
        NativeRuntime.printString("pressed:" + keyCode);
        NativeRuntime.printString("left-code:" + getKeyCode(LEFT));
    }

    protected void keyReleased(int keyCode) {
        NativeRuntime.printString("released:" + keyCode);
    }
}
