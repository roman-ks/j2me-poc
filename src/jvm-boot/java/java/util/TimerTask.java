package java.util;

public abstract class TimerTask implements Runnable {
    protected TimerTask() {}
    public abstract void run();
    public boolean cancel() { return false; }
    public long scheduledExecutionTime() { return 0L; }
}
