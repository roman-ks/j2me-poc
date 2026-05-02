package dev.roman.hello;

public class LoopWhile {
    public static void main(String[] args) {
        int sum = 0;
        int i = 0;

        while (i < 4) {
            sum = sum + i;
            i++;
        }

        NativeRuntime.printInt(sum);
    }
}
