package dev.roman.hello;

public class StringSubstring {
    public static void main(String[] args) {
        String text = "alpha|beta|gamma";
        NativeRuntime.printString(text.substring(0, 5));
        NativeRuntime.printString(text.substring(6, 10));
        NativeRuntime.printString(text.substring(11));
    }
}