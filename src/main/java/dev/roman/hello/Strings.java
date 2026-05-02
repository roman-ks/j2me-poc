package dev.roman.hello;

public class Strings {
    static String saved;

    static String echo(String value) {
        return value;
    }

    public static void main(String[] args) {
        String hello = "hello";
        saved = echo(hello);
        NativeRuntime.printString(saved);
        NativeRuntime.printString("literal");
        NativeRuntime.gc();
    }
}
