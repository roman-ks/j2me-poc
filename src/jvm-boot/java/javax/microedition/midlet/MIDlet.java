package javax.microedition.midlet;

public abstract class MIDlet {
    public MIDlet() {
    }

    protected abstract void startApp();

    protected abstract void pauseApp();

    protected abstract void destroyApp(boolean unconditional);

    public final native String getAppProperty(String key);
}
