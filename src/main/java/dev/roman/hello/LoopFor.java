package dev.roman.hello;

public class LoopFor {
    public static void main(String[] args) {
        int sum = 0;

        for (int i = 0; i < 4; i++) {
            sum = sum + i;
        }

        NativeRuntime.printInt(sum);
    }
}
