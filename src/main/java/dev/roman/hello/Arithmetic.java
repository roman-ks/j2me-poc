package dev.roman.hello;

public class Arithmetic {
    public static void main(String[] args) {
        int a = 20;
        int b = 5;

        NativeRuntime.printInt(a + b);
        NativeRuntime.printInt(a - b);
        NativeRuntime.printInt(a * b);
        NativeRuntime.printInt(a / b);
    }
}
