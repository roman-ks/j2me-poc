package dev.roman.hello;

public class StringConstructors {
    public static void main(String[] args) {
        char[] text = new char[5];
        text[0] = 'h';
        text[1] = 'e';
        text[2] = 'l';
        text[3] = 'l';
        text[4] = 'o';

        String slice = new String(text, 1, 3);
        String copy = new String(slice);
        String empty = new String();

        NativeRuntime.printString(slice);
        NativeRuntime.printString(copy);
        NativeRuntime.printInt(copy.length());
        NativeRuntime.printInt(empty.length());
    }
}