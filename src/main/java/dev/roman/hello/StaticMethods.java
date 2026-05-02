package dev.roman.hello;

public class StaticMethods {
    static int add(int a, int b) {
        return a + b;
    }

    static int twice(int value) {
        return value * 2;
    }

    static int choose(int value) {
        if (value > 5) {
            return value - 1;
        }
        return value + 1;
    }

    public static void main(String[] args) {
        int sum = add(2, 3);
        int result = choose(twice(sum));
        NativeRuntime.printInt(result);
    }
}
