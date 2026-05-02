package dev.roman.hello;

public class StaticFields {
    static int value;
    static int other;

    static void setValue(int x) {
        value = x;
    }

    static int getValue() {
        return value;
    }

    static void addToOther(int x) {
        other = other + x;
    }

    public static void main(String[] args) {
        setValue(42);
        addToOther(3);
        addToOther(4);
        NativeRuntime.printInt(getValue());
        NativeRuntime.printInt(other);
    }
}
