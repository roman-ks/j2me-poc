package javax.microedition.lcdui;

public class Image {
    protected Image() {
    }

    public static native Image createImage(String name);

    public static native Image createImage(int width, int height);

    public native int getWidth();

    public native int getHeight();

    public native Graphics getGraphics();
}
