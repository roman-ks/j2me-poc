package javax.microedition.lcdui;

public final class Graphics {
    public static final int HCENTER = 1;
    public static final int VCENTER = 2;
    public static final int LEFT = 4;
    public static final int RIGHT = 8;
    public static final int TOP = 16;
    public static final int BOTTOM = 32;
    public static final int BASELINE = 64;

    private Graphics() {
    }

    public native void setColor(int rgb);

    public native void setColor(int red, int green, int blue);

    public native void fillRect(int x, int y, int width, int height);

    public native void drawString(String str, int x, int y, int anchor);

    public native void drawImage(Image img, int x, int y, int anchor);
}
