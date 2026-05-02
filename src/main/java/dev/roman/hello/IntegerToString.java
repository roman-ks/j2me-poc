package dev.roman.hello;

public class IntegerToString {
    public static void main(String[] args) {
        NativeRuntime.printString(Integer.toString(1) + "[0].en");
        NativeRuntime.printString(Integer.toString(3) + ".sn");
    }
}