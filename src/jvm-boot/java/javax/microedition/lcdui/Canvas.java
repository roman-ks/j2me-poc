package javax.microedition.lcdui;

public abstract class Canvas extends Displayable {
    public static final int UP = 1;
    public static final int LEFT = 2;
    public static final int RIGHT = 5;
    public static final int DOWN = 6;
    public static final int FIRE = 8;
    public static final int GAME_A = 9;
    public static final int GAME_B = 10;
    public static final int GAME_C = 11;
    public static final int GAME_D = 12;

    public static final int KEY_NUM0 = 48;
    public static final int KEY_NUM1 = 49;
    public static final int KEY_NUM2 = 50;
    public static final int KEY_NUM3 = 51;
    public static final int KEY_NUM4 = 52;
    public static final int KEY_NUM5 = 53;
    public static final int KEY_NUM6 = 54;
    public static final int KEY_NUM7 = 55;
    public static final int KEY_NUM8 = 56;
    public static final int KEY_NUM9 = 57;
    public static final int KEY_STAR = 42;
    public static final int KEY_POUND = 35;

    public Canvas() {
    }

    public native void repaint();

    public native void repaint(int x, int y, int width, int height);

    public native void serviceRepaints();

    public native int getWidth();

    public native int getHeight();

    public native void setFullScreenMode(boolean mode);

    public int getKeyCode(int gameAction) {
        if (gameAction == UP) {
            return -1;
        }
        if (gameAction == DOWN) {
            return -2;
        }
        if (gameAction == LEFT) {
            return -3;
        }
        if (gameAction == RIGHT) {
            return -4;
        }
        if (gameAction == FIRE) {
            return -5;
        }
        if (gameAction == GAME_A) {
            return -6;
        }
        if (gameAction == GAME_B) {
            return -7;
        }
        if (gameAction == GAME_C) {
            return KEY_STAR;
        }
        if (gameAction == GAME_D) {
            return KEY_POUND;
        }
        return gameAction;
    }

    protected void keyPressed(int keyCode) {
    }

    protected void keyReleased(int keyCode) {
    }

    protected abstract void paint(Graphics g);
}
