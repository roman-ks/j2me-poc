package dev.roman.hello;

public class BooleanOps {
    public static void main(String[] args) {
        int a = 5;
        int b = 10;

        boolean less = a < b;
        boolean same = a == b;
        boolean notSame = !same;
        boolean both = less && notSame;
        boolean either = same || less;

        if (less) {
            NativeRuntime.printInt(1);
        } else {
            NativeRuntime.printInt(0);
        }

        if (notSame) {
            NativeRuntime.printInt(2);
        } else {
            NativeRuntime.printInt(3);
        }

        if (both) {
            NativeRuntime.printInt(4);
        } else {
            NativeRuntime.printInt(5);
        }

        if (either) {
            NativeRuntime.printInt(6);
        } else {
            NativeRuntime.printInt(7);
        }
    }
}
