package dev.roman.hello;

public class StringLength {
    public static void main(String[] args) {
        String value = "abc";
        NativeRuntime.printInt(value.length());
        NativeRuntime.printInt("hello".length());
    }
}
