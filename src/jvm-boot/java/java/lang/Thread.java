package java.lang;

public class Thread {
    private Runnable target;

    public Thread() {
    }

    public Thread(Runnable target) {
        this.target = target;
    }

    public native void start();

    public static native void yield();

    public static native void sleep(int millis);

    public static native void sleep(long millis);
}