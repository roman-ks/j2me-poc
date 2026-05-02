package dev.roman.hello;

final class ThreadBootTask implements Runnable {
    public void run() {
        NativeRuntime.printString("thread-run");
    }
}

public final class ThreadBoot {
    public static void main(String[] args) {
        Thread thread = new Thread(new ThreadBootTask());
        thread.start();
        try {
            Thread.sleep(0);
        } catch (InterruptedException e) {
            NativeRuntime.printString("interrupted");
        }
        NativeRuntime.printString("main-done");
    }
}