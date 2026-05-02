package dev.roman.hello;

public final class NativeRuntime {
    private NativeRuntime() {
    }

    public static native void printInt(int value);

    public static native void printString(String value);

    public static native void gc();
}
