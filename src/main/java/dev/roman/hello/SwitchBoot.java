package dev.roman.hello;

public final class SwitchBoot {
    private static int dense(int value) {
        switch (value) {
            case -1:
                return 7;
            case 0:
                return 3;
            case 1:
                return 5;
            case 2:
                return 9;
            default:
                return 11;
        }
    }

    private static int sparse(int value) {
        switch (value) {
            case -100:
                return 1;
            case 5:
                return 2;
            case 1000:
                return 3;
            default:
                return 4;
        }
    }

    public static void main(String[] args) {
        NativeRuntime.printInt(dense(-1));
        NativeRuntime.printInt(dense(2));
        NativeRuntime.printInt(dense(8));
        NativeRuntime.printInt(sparse(-100));
        NativeRuntime.printInt(sparse(5));
        NativeRuntime.printInt(sparse(6));
    }
}
