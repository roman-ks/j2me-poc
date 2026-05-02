package dev.roman.hello;

public class StringValueOf {
    public static void main(String[] args) {
        NativeRuntime.printString(String.valueOf(0));
        NativeRuntime.printString("/" + String.valueOf(1000) + ".map");
    }
}