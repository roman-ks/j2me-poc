package dev.roman.hello;

public class IntArrays {
    static int sum(int[] values) {
        int total = 0;

        for (int i = 0; i < values.length; i++) {
            total = total + values[i];
        }

        return total;
    }

    public static void main(String[] args) {
        int[] values = new int[4];
        values[0] = 3;
        values[1] = 4;
        values[2] = values[0] + values[1];
        values[3] = values.length;

        NativeRuntime.printInt(sum(values));
    }
}
