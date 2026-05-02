package javax.microedition.lcdui;

public abstract class Canvas extends Displayable {
    public Canvas() {
    }

    public native int getWidth();

    public native int getHeight();

    public native void setFullScreenMode(boolean mode);

    protected abstract void paint(Graphics g);
}
