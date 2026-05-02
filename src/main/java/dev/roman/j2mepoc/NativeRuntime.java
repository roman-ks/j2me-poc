package dev.roman.j2mepoc;

public final class NativeRuntime {
    private NativeRuntime() {
    }

    public static native void printInt(int value);
}
