package dev.roman.hello;

public class StringUnsupported {
    public static void main(String[] args) {
        String value = "abc";
        NativeRuntime.printInt(value.length());
    }
}
