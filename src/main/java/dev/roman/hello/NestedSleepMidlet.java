package dev.roman.hello;

import javax.microedition.midlet.MIDlet;

final class NestedSleepTask implements Runnable {
    public void run() {
        NativeRuntime.printString("before");
        inner();
        NativeRuntime.printString("after");
    }

    private void inner() {
        try {
            Thread.sleep(1L);
        } catch (InterruptedException e) {
            NativeRuntime.printString("interrupted");
        }
    }
}

public final class NestedSleepMidlet extends MIDlet {
    public void startApp() {
        new Thread(new NestedSleepTask()).start();
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}
