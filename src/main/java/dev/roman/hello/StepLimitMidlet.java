package dev.roman.hello;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

final class StepLimitTask implements Runnable {
    public void run() {
        while (true) {
        }
    }
}

final class StepLimitCanvas extends Canvas {
    protected void paint(Graphics g) {
        g.setColor(255, 255, 255);
        g.fillRect(0, 0, getWidth(), getHeight());
        g.setColor(0, 0, 0);
        g.fillRect(2, 2, 1, 1);
    }
}

public final class StepLimitMidlet extends MIDlet {
    public void startApp() {
        Display.getDisplay(this).setCurrent(new StepLimitCanvas());
        new Thread(new StepLimitTask()).start();
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}
