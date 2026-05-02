package dev.roman.j2mepoc;

import javax.microedition.lcdui.Display;
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;


public class HelloMidletMini extends MIDlet {
    MyCanvas canvas = new MyCanvas();

    public void startApp() {
        Display display = Display.getDisplay(this);
        display.setCurrent(canvas);
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean var1) {
    }

    private class MyCanvas extends Canvas {
        public MyCanvas() {
        }

        protected void paint(Graphics g) {
            g.setColor(255, 255, 255);
            g.fillRect(0, 0, this.getWidth(), this.getHeight());
            g.setColor(0, 0, 0);
            g.drawString("Hello World!", this.getWidth() / 2, this.getHeight() / 2, Graphics.HCENTER | Graphics.BASELINE);
        }
    } 
}
