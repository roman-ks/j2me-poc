package javax.microedition.lcdui;

public abstract class Canvas extends Displayable {
    public Canvas() {
    }

    public native void repaint();

    public native void repaint(int x, int y, int width, int height);

    public native void serviceRepaints();

    public native int getWidth();

    public native int getHeight();

    public native void setFullScreenMode(boolean mode);

    protected void keyPressed(int keyCode) {
    }

    protected void keyReleased(int keyCode) {
    }

    protected abstract void paint(Graphics g);
}
