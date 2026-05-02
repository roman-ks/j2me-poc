package dev.roman.hello;

public final class LongArithmetic {
    public static void main(String[] args) {
        long value = 20L;
        NativeRuntime.printLong(value + 4L);
        NativeRuntime.printLong(value - 3L);
        NativeRuntime.printLong(6L * 7L);
        NativeRuntime.printLong(value / 3L);
        NativeRuntime.printLong(value % 3L);
        NativeRuntime.printLong(-5L);
    }
}