package dev.roman.hello;

public class StringDigits {
    public static void main(String[] args) {
        String digits = "21";
        char[] chars = digits.toCharArray();
        int[] values = new int[chars.length];
        for (int i = 0; i < chars.length; i++) {
            values[i] = Integer.parseInt(String.valueOf(chars[i]));
        }

        NativeRuntime.printInt(values[0]);
        NativeRuntime.printInt(values[1]);
        NativeRuntime.printString("sn" + (values[0] - 1));
    }
}