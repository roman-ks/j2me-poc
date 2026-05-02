package dev.roman.hello;

public class LoopDoWhile {
    public static void main(String[] args) {
        int sum = 0;
        int i = 0;

        do {
            sum = sum + i;
            i++;
        } while (i < 4);

        NativeRuntime.printInt(sum);
    }
}
