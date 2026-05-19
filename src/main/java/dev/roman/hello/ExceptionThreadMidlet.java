package dev.roman.hello;

import javax.microedition.midlet.MIDlet;

final class ExceptionThreadTask implements Runnable {
    public void run() {
        throw new RuntimeException();
    }
}

public final class ExceptionThreadMidlet extends MIDlet {
    public void startApp() {
        Thread thread = new Thread(new ExceptionThreadTask());
        thread.start();
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }
}
