package java.lang;

public class Thread {
    private Runnable target;

    public Thread() {
    }

    public Thread(Runnable target) {
        this.target = target;
    }

    public void start() {
        if (target != null) {
            target.run();
        }
    }

    public static native void sleep(int millis);

    public static native void sleep(long millis);
}