package dev.roman.hello;

public class StringBytes {
    public static void main(String[] args) {
        byte[] data = new byte[10];
        data[0] = 'a';
        data[1] = 'l';
        data[2] = 'p';
        data[3] = 'h';
        data[4] = 'a';
        data[5] = '|';
        data[6] = 'b';
        data[7] = 'e';
        data[8] = 't';
        data[9] = 'a';

        NativeRuntime.printString(new String(data));
        NativeRuntime.printString(new String(data, 6, 4));
    }
}